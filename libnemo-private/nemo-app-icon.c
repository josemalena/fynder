/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Icono de paquetes de Mac.
 */

#include <config.h>

#include "nemo-app-icon.h"
#include "nemo-ds-store.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

#define MAX_ICNS_SIZE   (16 * 1024 * 1024)
#define MAX_PLIST_SIZE  (1024 * 1024)

static const char *package_extensions[] = {
	".app", ".bundle", ".framework", ".plugin", ".kext", ".prefpane", ".saver",
	".qlgenerator", ".mdimporter", ".xpc", ".appex", ".docset", ".wdgt", ".rtfd",
	".playground", ".xcarchive", ".xcodeproj", ".xcworkspace", ".xcassets",
	".pages", ".key", ".numbers", ".photoslibrary", ".imovielibrary", ".fcpbundle",
	".logicx", ".band", ".vmwarevm", ".pvm", ".sparsebundle", ".theme", NULL
};

gboolean
nemo_app_bundle_has_extension (const char *name)
{
	gsize len = strlen (name);

	for (guint i = 0; package_extensions[i] != NULL; i++) {
		gsize elen = strlen (package_extensions[i]);

		if (len > elen && g_ascii_strcasecmp (name + len - elen, package_extensions[i]) == 0)
			return TRUE;
	}
	return FALSE;
}

/* ---------- lectura de un archivo dentro de un zip (sin zip64) ---------- */

static guint32
le32 (const guchar *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((guint32) p[3] << 24);
}

static guint16
le16 (const guchar *p)
{
	return p[0] | (p[1] << 8);
}

typedef struct {
	char   *name;
	guint32 method, csize, usize, header_offset;
} ZipEntry;

static void
zip_entry_free (ZipEntry *e)
{
	g_free (e->name);
	g_free (e);
}

static GPtrArray *
zip_read_directory (GFileInputStream *in, goffset size)
{
	GSeekable *seek = G_SEEKABLE (in);
	gsize tail_len = MIN (size, 65557);
	guchar *tail = g_malloc (tail_len);
	gssize got;
	GPtrArray *entries = NULL;
	gssize i;
	guint32 cd_size = 0, cd_off = 0;
	guint total = 0;
	guchar *cd;

	if (!g_seekable_seek (seek, size - tail_len, G_SEEK_SET, NULL, NULL))
		goto out;
	got = g_input_stream_read (G_INPUT_STREAM (in), tail, tail_len, NULL, NULL);
	if (got < 22)
		goto out;

	for (i = got - 22; i >= 0; i--) {
		if (le32 (tail + i) == 0x06054b50) {
			total = le16 (tail + i + 10);
			cd_size = le32 (tail + i + 12);
			cd_off = le32 (tail + i + 16);
			break;
		}
	}
	if (i < 0 || cd_size == 0 || cd_size > 16 * 1024 * 1024 || cd_off == 0xFFFFFFFF)
		goto out;

	cd = g_malloc (cd_size);
	if (!g_seekable_seek (seek, cd_off, G_SEEK_SET, NULL, NULL) ||
	    g_input_stream_read_all (G_INPUT_STREAM (in), cd, cd_size, NULL, NULL, NULL) == FALSE) {
		g_free (cd);
		goto out;
	}

	entries = g_ptr_array_new_with_free_func ((GDestroyNotify) zip_entry_free);
	for (gsize p = 0; entries->len < total && p + 46 <= cd_size && le32 (cd + p) == 0x02014b50; ) {
		guint nlen = le16 (cd + p + 28), xlen = le16 (cd + p + 30), clen = le16 (cd + p + 32);
		ZipEntry *e;

		if (p + 46 + nlen > cd_size)
			break;
		e = g_new0 (ZipEntry, 1);
		e->method = le16 (cd + p + 10);
		e->csize = le32 (cd + p + 20);
		e->usize = le32 (cd + p + 24);
		e->header_offset = le32 (cd + p + 42);
		e->name = g_strndup ((const char *) cd + p + 46, nlen);
		g_ptr_array_add (entries, e);
		p += 46 + nlen + xlen + clen;
	}
	g_free (cd);
out:
	g_free (tail);
	return entries;
}

static ZipEntry *
zip_find (GPtrArray *entries, const char *suffix_path)
{
	for (guint i = 0; i < entries->len; i++) {
		ZipEntry *e = g_ptr_array_index (entries, i);

		/* «Paquete.app/<suffix_path>» o «<suffix_path>» a secas */
		if (g_str_has_suffix (e->name, suffix_path)) {
			const char *head = e->name + strlen (e->name) - strlen (suffix_path);
			const char *slash;

			/* solo el primer nivel: «Paquete.app/Contents/…», no el de un plugin anidado */
			if (head == e->name)
				return e;
			if (head[-1] != '/')
				continue;
			slash = memchr (e->name, '/', head - 1 - e->name);
			if (slash == NULL)
				return e;
		}
	}
	return NULL;
}

static GBytes *
zip_extract (GFileInputStream *in, ZipEntry *e, gsize max)
{
	guchar lh[30];
	guchar *comp, *out;
	gsize out_len;
	GBytes *result = NULL;

	if (e->usize > max || e->csize > max || e->csize == 0xFFFFFFFF)
		return NULL;
	if (!g_seekable_seek (G_SEEKABLE (in), e->header_offset, G_SEEK_SET, NULL, NULL) ||
	    !g_input_stream_read_all (G_INPUT_STREAM (in), lh, 30, NULL, NULL, NULL) ||
	    le32 (lh) != 0x04034b50)
		return NULL;
	if (!g_seekable_seek (G_SEEKABLE (in), le16 (lh + 26) + le16 (lh + 28), G_SEEK_CUR, NULL, NULL))
		return NULL;

	comp = g_malloc (e->csize);
	if (!g_input_stream_read_all (G_INPUT_STREAM (in), comp, e->csize, NULL, NULL, NULL)) {
		g_free (comp);
		return NULL;
	}
	if (e->method == 0) {
		return g_bytes_new_take (comp, e->csize);
	} else if (e->method == 8) {
		GZlibDecompressor *dec = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_RAW);
		gsize in_used = 0, out_used = 0;
		GConverterResult r;

		out = g_malloc (e->usize + 1);
		r = g_converter_convert (G_CONVERTER (dec), comp, e->csize, out, e->usize + 1,
					 G_CONVERTER_INPUT_AT_END, &in_used, &out_used, NULL);
		(void) r;
		out_len = out_used;
		g_object_unref (dec);
		g_free (comp);
		if (out_len == e->usize)
			result = g_bytes_new_take (out, out_len);
		else
			g_free (out);
		return result;
	}
	g_free (comp);
	return NULL;
}

/* ---------- Info.plist e .icns ---------- */

static char *
icon_name_from_plist (GBytes *plist)
{
	gsize len;
	const char *data = g_bytes_get_data (plist, &len);
	char *result = NULL;

	if (len > 8 && memcmp (data, "bplist00", 8) == 0) {
		GVariant *v = nemo_bplist_parse ((const guchar *) data, len);

		if (v != NULL) {
			if (g_variant_is_of_type (v, G_VARIANT_TYPE ("a{sv}"))) {
				const char *str = NULL;

				if (g_variant_lookup (v, "CFBundleIconFile", "&s", &str) && str != NULL)
					result = g_strdup (str);
			}
			g_variant_unref (v);
		}
	} else {
		char *text = g_strndup (data, len);
		const char *key = strstr (text, "<key>CFBundleIconFile</key>");

		if (key != NULL) {
			const char *s = strstr (key, "<string>");

			if (s != NULL) {
				const char *e = strstr (s + 8, "</string>");

				if (e != NULL)
					result = g_strndup (s + 8, e - (s + 8));
			}
		}
		g_free (text);
	}
	if (result != NULL && result[0] == '\0')
		g_clear_pointer (&result, g_free);
	return result;
}

static guint32
be32 (const guchar *p)
{
	return ((guint32) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

/* Del .icns se toma el PNG de tamaño razonable; si solo trae formatos viejos,
 * gdk-pixbuf decodifica el archivo entero. Devuelve los bytes de un PNG. */
static GBytes *
icns_to_png (GBytes *icns)
{
	static const char *wanted[] = { "ic08", "ic09", "ic07", "ic13", "ic14", "ic10", "ic12", "ic11", NULL };
	gsize len;
	const guchar *d = g_bytes_get_data (icns, &len);
	GdkPixbufLoader *loader;
	GdkPixbuf *pixbuf = NULL;
	GBytes *result = NULL;

	if (len < 16 || memcmp (d, "icns", 4) != 0)
		return NULL;

	for (guint w = 0; wanted[w] != NULL; w++) {
		for (gsize p = 8; p + 8 <= len; ) {
			guint32 elen = be32 (d + p + 4);

			if (elen < 8 || p + elen > len)
				break;
			if (memcmp (d + p, wanted[w], 4) == 0 && elen > 16 &&
			    memcmp (d + p + 8, "\x89PNG", 4) == 0)
				return g_bytes_new (d + p + 8, elen - 8);
			p += elen;
		}
	}

	loader = gdk_pixbuf_loader_new_with_type ("icns", NULL);
	if (loader != NULL) {
		if (gdk_pixbuf_loader_write (loader, d, len, NULL) && gdk_pixbuf_loader_close (loader, NULL))
			pixbuf = g_object_ref (gdk_pixbuf_loader_get_pixbuf (loader));
		g_object_unref (loader);
	}
	if (pixbuf != NULL) {
		gchar *buf = NULL;
		gsize buf_len = 0;

		if (gdk_pixbuf_save_to_buffer (pixbuf, &buf, &buf_len, "png", NULL, NULL))
			result = g_bytes_new_take (buf, buf_len);
		g_object_unref (pixbuf);
	}
	return result;
}

/* ---------- lectura según sea carpeta o zip ---------- */

static GBytes *
read_file_bytes (const char *path, gsize max)
{
	gchar *data = NULL;
	gsize len = 0;

	if (!g_file_get_contents (path, &data, &len, NULL))
		return NULL;
	if (len > max) {
		g_free (data);
		return NULL;
	}
	return g_bytes_new_take (data, len);
}

static char *
icon_file_name (const char *icon_name)
{
	return g_str_has_suffix (icon_name, ".icns") ? g_strdup (icon_name)
						     : g_strconcat (icon_name, ".icns", NULL);
}

static GBytes *
icns_from_directory (const char *path)
{
	char *plist_path = g_build_filename (path, "Contents", "Info.plist", NULL);
	GBytes *plist = read_file_bytes (plist_path, MAX_PLIST_SIZE);
	GBytes *icns = NULL;
	char *name = plist != NULL ? icon_name_from_plist (plist) : NULL;

	g_free (plist_path);
	if (plist != NULL)
		g_bytes_unref (plist);
	if (name == NULL)
		name = g_strdup ("AppIcon");
	{
		char *file = icon_file_name (name);
		char *icns_path = g_build_filename (path, "Contents", "Resources", file, NULL);

		icns = read_file_bytes (icns_path, MAX_ICNS_SIZE);
		g_free (icns_path);
		g_free (file);
	}
	g_free (name);
	return icns;
}

static GBytes *
icns_from_zip (const char *path)
{
	GFile *file = g_file_new_for_path (path);
	GFileInputStream *in = g_file_read (file, NULL, NULL);
	GFileInfo *info = NULL;
	GPtrArray *entries = NULL;
	GBytes *icns = NULL;

	g_object_unref (file);
	if (in == NULL)
		return NULL;
	info = g_file_input_stream_query_info (in, G_FILE_ATTRIBUTE_STANDARD_SIZE, NULL, NULL);
	if (info != NULL && g_file_info_get_size (info) > 22)
		entries = zip_read_directory (in, g_file_info_get_size (info));
	if (entries != NULL) {
		ZipEntry *pe = zip_find (entries, "Contents/Info.plist");
		GBytes *plist = pe != NULL ? zip_extract (in, pe, MAX_PLIST_SIZE) : NULL;
		char *name = plist != NULL ? icon_name_from_plist (plist) : NULL;
		char *file_name, *suffix;
		ZipEntry *ie;

		if (plist != NULL)
			g_bytes_unref (plist);
		if (name == NULL)
			name = g_strdup ("AppIcon");
		file_name = icon_file_name (name);
		suffix = g_strconcat ("Contents/Resources/", file_name, NULL);
		ie = zip_find (entries, suffix);
		if (ie != NULL)
			icns = zip_extract (in, ie, MAX_ICNS_SIZE);
		g_free (suffix);
		g_free (file_name);
		g_free (name);
		g_ptr_array_unref (entries);
	}
	g_clear_object (&info);
	g_object_unref (in);
	return icns;
}

GFile *
nemo_app_bundle_icon_file (const char *path, gboolean is_directory, gint64 mtime)
{
	char *key = g_strdup_printf ("v2:%s:%" G_GINT64_FORMAT, path, mtime);
	char *sum = g_compute_checksum_for_string (G_CHECKSUM_SHA1, key, -1);
	char *dir = g_build_filename (g_get_user_cache_dir (), "fynder", "app-icons", NULL);
	char *name = g_strconcat (sum, ".png", NULL);
	char *cached = g_build_filename (dir, name, NULL);
	char *missing = g_strconcat (cached, ".none", NULL);
	GFile *result = NULL;

	g_free (key);
	g_free (sum);
	g_free (name);

	if (g_file_test (cached, G_FILE_TEST_EXISTS)) {
		result = g_file_new_for_path (cached);
	} else if (!g_file_test (missing, G_FILE_TEST_EXISTS)) {
		GBytes *icns = is_directory ? icns_from_directory (path) : icns_from_zip (path);
		GBytes *png = icns != NULL ? icns_to_png (icns) : NULL;

		g_mkdir_with_parents (dir, 0755);
		if (png != NULL) {
			gsize len;
			const char *data = g_bytes_get_data (png, &len);

			if (g_file_set_contents (cached, data, len, NULL))
				result = g_file_new_for_path (cached);
		} else {
			g_file_set_contents (missing, "", 0, NULL); /* no volver a intentarlo */
		}
		if (icns != NULL)
			g_bytes_unref (icns);
		if (png != NULL)
			g_bytes_unref (png);
	}
	g_free (dir);
	g_free (cached);
	g_free (missing);
	return result;
}
