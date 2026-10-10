/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Lector del .DS_Store de macOS (ver nemo-ds-store.h).
 */

#include <config.h>

#include "nemo-ds-store.h"

#include <string.h>

struct _NemoDSStore {
	GBytes *bytes;
	const guchar *data;
	gsize size;
	GHashTable *entries;   /* "archivo\x1fcódigo" -> GVariant (ref propia) */
	GHashTable *files;     /* nombres vistos */
};

/* ---------- lectura segura ---------- */

static gboolean
read_u32 (const guchar *data, gsize size, gsize offset, guint32 *out)
{
	if (offset + 4 > size)
		return FALSE;
	*out = ((guint32) data[offset] << 24) | ((guint32) data[offset + 1] << 16) |
	       ((guint32) data[offset + 2] << 8) | data[offset + 3];
	return TRUE;
}

/* ---------- bloques (asignador «buddy») ---------- */

typedef struct {
	NemoDSStore *store;
	guint32 *offsets;
	guint32 n_offsets;
} Buddy;

/* Posición en el archivo y tamaño de un bloque; los desplazamientos van
 * relativos al byte 4 del archivo. */
static gboolean
buddy_block (Buddy *b, guint32 number, gsize *position, gsize *length)
{
	guint32 value;

	if (number >= b->n_offsets)
		return FALSE;
	value = b->offsets[number];
	if (value == 0)
		return FALSE;

	*position = (gsize) (value & ~0x1fu) + 4;
	*length = (gsize) 1 << (value & 0x1f);
	return *position + *length <= b->store->size + 0; /* dentro del archivo */
}

/* ---------- registros ---------- */

static void
store_entry (NemoDSStore *store, const char *filename, const char *code, GVariant *value)
{
	char *key = g_strdup_printf ("%s\x1f%s", filename, code);

	g_hash_table_replace (store->entries, key, g_variant_ref_sink (value));
	if (g_strcmp0 (filename, ".") != 0 && !g_hash_table_contains (store->files, filename))
		g_hash_table_add (store->files, g_strdup (filename));
}

static char *
utf16be_to_utf8 (const guchar *data, gsize chars)
{
	gunichar2 *u16 = g_new (gunichar2, chars + 1);
	gsize i;
	char *utf8;

	for (i = 0; i < chars; i++)
		u16[i] = (data[2 * i] << 8) | data[2 * i + 1];
	u16[chars] = 0;
	utf8 = g_utf16_to_utf8 (u16, chars, NULL, NULL, NULL);
	g_free (u16);
	return utf8;
}

/* Lee un registro en `*pos` (dentro de [0, end)); avanza `*pos`. FALSE si es inválido. */
static gboolean
read_record (NemoDSStore *store, const guchar *block, gsize end, gsize *pos)
{
	guint32 name_len, length;
	char *filename;
	char code[5], type[5];
	GVariant *value = NULL;
	gsize p = *pos;

	if (!read_u32 (block, end, p, &name_len) || name_len > 1024)
		return FALSE;
	p += 4;
	if (p + (gsize) name_len * 2 + 8 > end)
		return FALSE;
	filename = utf16be_to_utf8 (block + p, name_len);
	if (filename == NULL)
		return FALSE;
	p += (gsize) name_len * 2;

	memcpy (code, block + p, 4); code[4] = '\0';
	memcpy (type, block + p + 4, 4); type[4] = '\0';
	p += 8;

	if (strcmp (type, "bool") == 0) {
		if (p + 1 > end) goto fail;
		value = g_variant_new_boolean (block[p] != 0);
		p += 1;
	} else if (strcmp (type, "long") == 0 || strcmp (type, "shor") == 0) {
		guint32 v;

		if (!read_u32 (block, end, p, &v)) goto fail;
		value = g_variant_new_int32 ((gint32) v);
		p += 4;
	} else if (strcmp (type, "type") == 0) {
		if (p + 4 > end) goto fail;
		{
			char *four = g_strndup ((const char *) block + p, 4);

			value = g_variant_new_string (four);
			g_free (four);
		}
		p += 4;
	} else if (strcmp (type, "comp") == 0 || strcmp (type, "dutc") == 0) {
		guint32 hi, lo;

		if (!read_u32 (block, end, p, &hi) || !read_u32 (block, end, p + 4, &lo)) goto fail;
		value = g_variant_new_int64 ((gint64) (((guint64) hi << 32) | lo));
		p += 8;
	} else if (strcmp (type, "blob") == 0) {
		if (!read_u32 (block, end, p, &length) || p + 4 + length > end) goto fail;
		value = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, block + p + 4, length, 1);
		p += 4 + length;
	} else if (strcmp (type, "ustr") == 0) {
		char *s;

		if (!read_u32 (block, end, p, &length) || length > 4096 || p + 4 + (gsize) length * 2 > end) goto fail;
		s = utf16be_to_utf8 (block + p + 4, length);
		value = g_variant_new_string (s != NULL ? s : "");
		g_free (s);
		p += 4 + (gsize) length * 2;
	} else {
		goto fail;
	}

	store_entry (store, filename, code, value);
	g_free (filename);
	*pos = p;
	return TRUE;

fail:
	g_free (filename);
	return FALSE;
}

/* Recorre un nodo del árbol B (recursivo). */
static void
walk_node (Buddy *b, guint32 number, int depth)
{
	gsize position, length, p;
	guint32 pointer, count, i;
	const guchar *block;

	if (depth > 16 || !buddy_block (b, number, &position, &length) ||
	    position + length > b->store->size)
		return;

	block = b->store->data + position;
	if (!read_u32 (block, length, 0, &pointer) || !read_u32 (block, length, 4, &count))
		return;
	p = 8;

	if (pointer == 0) {
		/* hoja: solo registros */
		for (i = 0; i < count; i++) {
			if (!read_record (b->store, block, length, &p))
				return;
		}
	} else {
		/* nodo interno: (hijo, registro) × count y luego el último hijo */
		for (i = 0; i < count; i++) {
			guint32 child;

			if (!read_u32 (block, length, p, &child))
				return;
			p += 4;
			walk_node (b, child, depth + 1);
			if (!read_record (b->store, block, length, &p))
				return;
		}
		walk_node (b, pointer, depth + 1);
	}
}

/* ---------- apertura ---------- */

NemoDSStore *
nemo_ds_store_open (const char *path)
{
	GError *error = NULL;
	GBytes *bytes;
	NemoDSStore *store;
	Buddy b = { 0 };
	const guchar *data;
	gsize size, root_off, root_size, p, i;
	guint32 u, n_offsets, n_toc, dsdb_block = 0, root_node;
	gboolean found = FALSE;
	gchar *contents = NULL;

	if (!g_file_get_contents (path, &contents, &size, &error)) {
		g_clear_error (&error);
		return NULL;
	}
	if (size < 36) {
		g_free (contents);
		return NULL;
	}

	bytes = g_bytes_new_take (contents, size);
	data = g_bytes_get_data (bytes, NULL);

	/* 00 00 00 01 'Bud1' offset size offset ... */
	if (memcmp (data, "\0\0\0\1Bud1", 8) != 0) {
		g_bytes_unref (bytes);
		return NULL;
	}

	store = g_new0 (NemoDSStore, 1);
	store->bytes = bytes;
	store->data = data;
	store->size = size;
	store->entries = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_variant_unref);
	store->files = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	b.store = store;

	if (!read_u32 (data, size, 8, &u))
		goto fail;
	root_off = u + 4;
	if (!read_u32 (data, size, 12, &u))
		goto fail;
	root_size = u;
	if (root_off + root_size > size)
		goto fail;

	/* bloque raíz: tabla de desplazamientos y directorio */
	if (!read_u32 (data, size, root_off, &n_offsets) || n_offsets > 1 << 20)
		goto fail;
	p = root_off + 8; /* n_offsets + 4 bytes reservados */
	b.n_offsets = n_offsets;
	b.offsets = g_new0 (guint32, n_offsets + 1);
	for (i = 0; i < n_offsets; i++) {
		if (!read_u32 (data, size, p + 4 * i, &b.offsets[i]))
			goto fail;
	}
	/* la tabla se rellena hasta un múltiplo de 256 entradas */
	p += 4 * (((gsize) n_offsets + 255) / 256 * 256);

	if (!read_u32 (data, size, p, &n_toc))
		goto fail;
	p += 4;
	for (i = 0; i < n_toc; i++) {
		guint8 name_len;
		char name[256];
		guint32 block;

		if (p + 1 > size)
			goto fail;
		name_len = data[p++];
		if (p + name_len + 4 > size)
			goto fail;
		memcpy (name, data + p, name_len);
		name[name_len] = '\0';
		p += name_len;
		read_u32 (data, size, p, &block);
		p += 4;
		if (strcmp (name, "DSDB") == 0) {
			dsdb_block = block;
			found = TRUE;
		}
	}
	if (!found)
		goto fail;

	/* bloque DSDB: nodo raíz, altura, nº de registros, nº de nodos, tamaño de página */
	{
		gsize position, length;

		if (!buddy_block (&b, dsdb_block, &position, &length) || position + 20 > size)
			goto fail;
		if (!read_u32 (data, size, position, &root_node))
			goto fail;
	}

	walk_node (&b, root_node, 0);
	g_free (b.offsets);
	return store;

fail:
	g_free (b.offsets);
	nemo_ds_store_free (store);
	return NULL;
}

void
nemo_ds_store_free (NemoDSStore *store)
{
	if (store == NULL)
		return;
	g_clear_pointer (&store->entries, g_hash_table_unref);
	g_clear_pointer (&store->files, g_hash_table_unref);
	g_clear_pointer (&store->bytes, g_bytes_unref);
	g_free (store);
}

GVariant *
nemo_ds_store_lookup (NemoDSStore *store, const char *filename, const char *code)
{
	char *key = g_strdup_printf ("%s\x1f%s", filename, code);
	GVariant *value = g_hash_table_lookup (store->entries, key);

	g_free (key);
	return value != NULL ? g_variant_ref (value) : NULL;
}

GVariant *
nemo_ds_store_lookup_plist (NemoDSStore *store, const char *filename, const char *code)
{
	GVariant *blob = nemo_ds_store_lookup (store, filename, code);
	GVariant *result = NULL;

	if (blob != NULL && g_variant_is_of_type (blob, G_VARIANT_TYPE ("ay"))) {
		gsize length;
		const guchar *data = g_variant_get_fixed_array (blob, &length, 1);

		result = nemo_bplist_parse (data, length);
	}
	if (blob != NULL)
		g_variant_unref (blob);
	return result;
}

gboolean
nemo_ds_store_get_icon_location (NemoDSStore *store, const char *filename, int *x, int *y)
{
	GVariant *blob = nemo_ds_store_lookup (store, filename, "Iloc");
	gboolean ok = FALSE;

	if (blob != NULL && g_variant_is_of_type (blob, G_VARIANT_TYPE ("ay"))) {
		gsize length;
		const guchar *d = g_variant_get_fixed_array (blob, &length, 1);

		/* x (u32), y (u32), y relleno 0xFFFFFFFF 0xFFFF0000 */
		if (length >= 8) {
			*x = (gint32) (((guint32) d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]);
			*y = (gint32) (((guint32) d[4] << 24) | (d[5] << 16) | (d[6] << 8) | d[7]);
			ok = TRUE;
		}
	}
	if (blob != NULL)
		g_variant_unref (blob);
	return ok;
}

GPtrArray *
nemo_ds_store_list_files (NemoDSStore *store)
{
	GPtrArray *list = g_ptr_array_new_with_free_func (g_free);
	GHashTableIter iter;
	gpointer key;

	g_hash_table_iter_init (&iter, store->files);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_ptr_array_add (list, g_strdup (key));
	return list;
}

/* ---------- plist binario ---------- */

typedef struct {
	const guchar *data;
	gsize length;
	gsize offset_size;
	gsize ref_size;
	guint64 n_objects;
	guint64 table;      /* inicio de la tabla de desplazamientos */
} Plist;

static guint64
read_be (const guchar *p, gsize n)
{
	guint64 v = 0;
	gsize i;

	for (i = 0; i < n; i++)
		v = (v << 8) | p[i];
	return v;
}

static GVariant *parse_object (Plist *pl, guint64 index, int depth);

static gboolean
object_offset (Plist *pl, guint64 index, gsize *offset)
{
	guint64 at;

	if (index >= pl->n_objects)
		return FALSE;
	at = pl->table + index * pl->offset_size;
	if (at + pl->offset_size > pl->length)
		return FALSE;
	*offset = (gsize) read_be (pl->data + at, pl->offset_size);
	return *offset < pl->length;
}

/* Longitud de un objeto: nibble bajo, o un entero aparte si vale 0xF. */
static gboolean
object_length (Plist *pl, gsize *pos, guchar marker, guint64 *length)
{
	guint64 n = marker & 0x0f;

	if (n == 0x0f) {
		guchar int_marker;
		gsize bytes;

		if (*pos + 1 > pl->length)
			return FALSE;
		int_marker = pl->data[(*pos)++];
		if ((int_marker & 0xf0) != 0x10)
			return FALSE;
		bytes = (gsize) 1 << (int_marker & 0x0f);
		if (bytes > 8 || *pos + bytes > pl->length)
			return FALSE;
		n = read_be (pl->data + *pos, bytes);
		*pos += bytes;
	}
	*length = n;
	return TRUE;
}

static GVariant *
parse_object (Plist *pl, guint64 index, int depth)
{
	gsize pos;
	guchar marker;
	guint64 n, i;

	if (depth > 32 || !object_offset (pl, index, &pos))
		return NULL;

	marker = pl->data[pos++];

	switch (marker >> 4) {
	case 0x0:
		if (marker == 0x08) return g_variant_new_boolean (FALSE);
		if (marker == 0x09) return g_variant_new_boolean (TRUE);
		return NULL;
	case 0x1: {
		gsize bytes = (gsize) 1 << (marker & 0x0f);

		/* macOS 27 escribe viewOptionsVersion como entero de 16 bytes: vale el valor de los
		 * últimos 8 */
		if (bytes > 16 || pos + bytes > pl->length)
			return NULL;
		if (bytes > 8)
			return g_variant_new_int64 ((gint64) read_be (pl->data + pos + bytes - 8, 8));
		return g_variant_new_int64 ((gint64) read_be (pl->data + pos, bytes));
	}
	case 0x2: {
		gsize bytes = (gsize) 1 << (marker & 0x0f);

		if (pos + bytes > pl->length)
			return NULL;
		if (bytes == 8) {
			guint64 bits = read_be (pl->data + pos, 8);
			double d;

			memcpy (&d, &bits, 8);
			return g_variant_new_double (d);
		} else if (bytes == 4) {
			guint32 bits = (guint32) read_be (pl->data + pos, 4);
			float f;

			memcpy (&f, &bits, 4);
			return g_variant_new_double (f);
		}
		return NULL;
	}
	case 0x3: /* fecha: segundos desde 2001 */
		if (pos + 8 > pl->length)
			return NULL;
		{
			guint64 bits = read_be (pl->data + pos, 8);
			double d;

			memcpy (&d, &bits, 8);
			return g_variant_new_double (d);
		}
	case 0x4:
		if (!object_length (pl, &pos, marker, &n) || pos + n > pl->length)
			return NULL;
		return g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, pl->data + pos, n, 1);
	case 0x5:
		if (!object_length (pl, &pos, marker, &n) || pos + n > pl->length)
			return NULL;
		{
			char *s = g_strndup ((const char *) pl->data + pos, n);
			GVariant *v = g_variant_new_string (g_utf8_validate (s, -1, NULL) ? s : "");

			g_free (s);
			return v;
		}
	case 0x6:
		if (!object_length (pl, &pos, marker, &n) || pos + n * 2 > pl->length)
			return NULL;
		{
			char *s = utf16be_to_utf8 (pl->data + pos, n);
			GVariant *v = g_variant_new_string (s != NULL ? s : "");

			g_free (s);
			return v;
		}
	case 0xa:
	case 0xc: { /* lista */
		GVariantBuilder builder;

		if (!object_length (pl, &pos, marker, &n) || pos + n * pl->ref_size > pl->length)
			return NULL;
		g_variant_builder_init (&builder, G_VARIANT_TYPE ("av"));
		for (i = 0; i < n; i++) {
			GVariant *item = parse_object (pl, read_be (pl->data + pos + i * pl->ref_size, pl->ref_size), depth + 1);

			if (item != NULL)
				g_variant_builder_add (&builder, "v", item);
		}
		return g_variant_builder_end (&builder);
	}
	case 0xd: { /* diccionario: n claves y luego n valores */
		GVariantBuilder builder;

		if (!object_length (pl, &pos, marker, &n) || pos + 2 * n * pl->ref_size > pl->length)
			return NULL;
		g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
		for (i = 0; i < n; i++) {
			GVariant *key = parse_object (pl, read_be (pl->data + pos + i * pl->ref_size, pl->ref_size), depth + 1);
			GVariant *value = parse_object (pl, read_be (pl->data + pos + (n + i) * pl->ref_size, pl->ref_size), depth + 1);

			if (key != NULL && value != NULL && g_variant_is_of_type (key, G_VARIANT_TYPE_STRING))
				g_variant_builder_add (&builder, "{sv}", g_variant_get_string (key, NULL), value);
			else if (value != NULL)
				g_variant_unref (g_variant_ref_sink (value));
			if (key != NULL)
				g_variant_unref (g_variant_ref_sink (key));
		}
		return g_variant_builder_end (&builder);
	}
	default:
		return NULL;
	}
}

GVariant *
nemo_bplist_parse (const guchar *data, gsize length)
{
	Plist pl;
	const guchar *trailer;
	guint64 top;
	GVariant *result;

	if (length < 8 + 32 || memcmp (data, "bplist00", 8) != 0)
		return NULL;

	trailer = data + length - 32;
	pl.data = data;
	pl.length = length;
	pl.offset_size = trailer[6];
	pl.ref_size = trailer[7];
	pl.n_objects = read_be (trailer + 8, 8);
	top = read_be (trailer + 16, 8);
	pl.table = read_be (trailer + 24, 8);

	if (pl.offset_size == 0 || pl.offset_size > 8 || pl.ref_size == 0 || pl.ref_size > 8 ||
	    pl.n_objects == 0 || pl.n_objects > 100000 || pl.table >= length)
		return NULL;

	result = parse_object (&pl, top, 0);
	return result != NULL ? g_variant_ref_sink (result) : NULL;
}
