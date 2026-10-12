/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Lector de etiquetas y comentarios de Finder (xattr o AppleDouble).
 */

#include <config.h>

#include "nemo-mac-metadata.h"
#include "nemo-ds-store.h"

#include <string.h>
#include <sys/xattr.h>
#include <gio/gio.h>

#define TAGS_ATTR    "com.apple.metadata:_kMDItemUserTags"
#define COMMENT_ATTR "com.apple.metadata:kMDItemFinderComment"

#define APPLEDOUBLE_MAGIC   0x00051607
#define APPLEDOUBLE_FINDER  9
#define APPLEDOUBLE_MAX     (4 * 1024 * 1024)

static GBytes *
read_native_xattr (const char *path, const char *name)
{
	const char *prefixes[] = { "user.", "" };

	for (guint i = 0; i < G_N_ELEMENTS (prefixes); i++) {
		char *full = g_strconcat (prefixes[i], name, NULL);
		ssize_t size = lgetxattr (path, full, NULL, 0);

		if (size > 0 && size < APPLEDOUBLE_MAX) {
			guchar *buf = g_malloc (size);
			ssize_t got = lgetxattr (path, full, buf, size);

			g_free (full);
			if (got > 0)
				return g_bytes_new_take (buf, got);
			g_free (buf);
			continue;
		}
		g_free (full);
	}
	return NULL;
}

static guint32
be32 (const guchar *p)
{
	return ((guint32) p[0] << 24) | ((guint32) p[1] << 16) | ((guint32) p[2] << 8) | p[3];
}

static guint16
be16 (const guchar *p)
{
	return (guint16) (((guint16) p[0] << 8) | p[1]);
}

/* AppleDouble: los atributos van dentro de la entrada 9 (FinderInfo), tras una
 * cabecera «ATTR». Los desplazamientos de cada atributo son del archivo entero. */
static GBytes *
read_appledouble_attr (const char *path, const char *name)
{
	char *dir = g_path_get_dirname (path);
	char *base = g_path_get_basename (path);
	char *sidecar_name = g_strconcat ("._", base, NULL);
	char *sidecar = g_build_filename (dir, sidecar_name, NULL);
	gchar *data = NULL;
	gsize length = 0;
	GBytes *result = NULL;

	g_free (dir);
	g_free (base);
	g_free (sidecar_name);

	if (!g_file_get_contents (sidecar, &data, &length, NULL)) {
		g_free (sidecar);
		return NULL;
	}
	g_free (sidecar);

	if (length > 26 && length <= APPLEDOUBLE_MAX && be32 ((guchar *) data) == APPLEDOUBLE_MAGIC) {
		const guchar *d = (const guchar *) data;
		guint n_entries = be16 (d + 24);

		for (guint i = 0; i < n_entries && 26 + (i + 1) * 12 <= length; i++) {
			const guchar *e = d + 26 + i * 12;
			guint32 id = be32 (e), off = be32 (e + 4), len = be32 (e + 8);
			gsize start, end;

			if (id != APPLEDOUBLE_FINDER || off > length || len > length - off)
				continue;

			/* tras los 32 bytes de FinderInfo (y algo de relleno) viene «ATTR» */
			end = off + len;
			for (start = off + 32; start + 4 <= MIN (end, off + 40); start++) {
				if (memcmp (d + start, "ATTR", 4) == 0)
					break;
			}
			if (start + 36 > end || memcmp (d + start, "ATTR", 4) != 0)
				break;

			{
				guint n_attrs = be16 (d + start + 34);
				gsize p = start + 36;

				for (guint a = 0; a < n_attrs && p + 11 <= end; a++) {
					guint32 a_off = be32 (d + p), a_len = be32 (d + p + 4);
					guint name_len = d[p + 10];

					if (p + 11 + name_len > end)
						break;
					if (name_len > 0 && strncmp ((const char *) d + p + 11, name, name_len) == 0 &&
					    a_off <= length && a_len <= length - a_off) {
						result = g_bytes_new (d + a_off, a_len);
						break;
					}
					p += (11 + name_len + 3) & ~3u;
				}
			}
			break;
		}
	}
	g_free (data);
	return result;
}

static GVariant *
read_plist_attr (const char *path, const char *name)
{
	GBytes *bytes = read_native_xattr (path, name);
	GVariant *plist;
	gsize size;
	const guchar *data;

	if (bytes == NULL)
		bytes = read_appledouble_attr (path, name);
	if (bytes == NULL)
		return NULL;

	data = g_bytes_get_data (bytes, &size);
	plist = nemo_bplist_parse (data, size);
	g_bytes_unref (bytes);
	return plist;
}

GList *
nemo_mac_metadata_get_tags (const char *path)
{
	GVariant *plist = read_plist_attr (path, TAGS_ATTR);
	GList *tags = NULL;
	GVariantIter iter;
	GVariant *item;

	if (plist == NULL)
		return NULL;
	if (g_variant_is_of_type (plist, G_VARIANT_TYPE ("av"))) {
		g_variant_iter_init (&iter, plist);
		while ((item = g_variant_iter_next_value (&iter)) != NULL) {
			GVariant *inner = g_variant_get_variant (item);

			if (g_variant_is_of_type (inner, G_VARIANT_TYPE_STRING)) {
				const char *s = g_variant_get_string (inner, NULL);
				const char *nl = strchr (s, '\n');
				NemoMacTag *tag = g_new0 (NemoMacTag, 1);

				tag->name = nl != NULL ? g_strndup (s, nl - s) : g_strdup (s);
				tag->color = nl != NULL ? atoi (nl + 1) : 0;
				tags = g_list_append (tags, tag);
			}
			g_variant_unref (inner);
			g_variant_unref (item);
		}
	}
	g_variant_unref (plist);
	return tags;
}

char *
nemo_mac_metadata_get_comment (const char *path)
{
	GVariant *plist = read_plist_attr (path, COMMENT_ATTR);
	char *comment = NULL;

	if (plist == NULL)
		return NULL;
	if (g_variant_is_of_type (plist, G_VARIANT_TYPE_STRING)) {
		const char *s = g_variant_get_string (plist, NULL);

		if (s[0] != '\0')
			comment = g_strdup (s);
	}
	g_variant_unref (plist);
	return comment;
}

void
nemo_mac_tags_free (GList *tags)
{
	for (GList *l = tags; l != NULL; l = l->next) {
		NemoMacTag *tag = l->data;

		g_free (tag->name);
		g_free (tag);
	}
	g_list_free (tags);
}

static const char *
color_hex (int color)
{
	switch (color) {
	case 1: return "#8e8e93";
	case 2: return "#34c759";
	case 3: return "#af52de";
	case 4: return "#0a84ff";
	case 5: return "#ffd60a";
	case 6: return "#ff453a";
	case 7: return "#ff9f0a";
	default: return NULL;
	}
}

char *
nemo_mac_tags_to_markup (GList *tags)
{
	GString *out = g_string_new (NULL);

	for (GList *l = tags; l != NULL; l = l->next) {
		NemoMacTag *tag = l->data;
		const char *hex = color_hex (tag->color);
		char *escaped = g_markup_escape_text (tag->name, -1);

		if (out->len > 0)
			g_string_append (out, "  ");
		if (hex != NULL)
			g_string_append_printf (out, "<span foreground=\"%s\">●</span> ", hex);
		g_string_append (out, escaped);
		g_free (escaped);
	}
	return g_string_free (out, FALSE);
}
