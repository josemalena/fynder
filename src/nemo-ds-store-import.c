/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Importación del .DS_Store de macOS (ver nemo-ds-store-import.h).
 */

#include <config.h>

#include "nemo-ds-store-import.h"

#include "nemo-icon-view.h"
#include "nemo-list-view.h"

#include <string.h>
#include <glib/gi18n.h>
#include <libnemo-private/nemo-ds-store.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-icon-info.h>
#include <libnemo-private/nemo-metadata.h>

#define IMPORTED_KEY "fynder-ds-store-imported"

/* Columnas del Finder -> columnas de Fynder (las que no existen se ignoran). */
static const struct {
	const char *finder;
	const char *fynder;
} column_map[] = {
	{ "name",           "name" },
	{ "size",           "size" },
	{ "kind",           "type" },
	{ "dateModified",   "date_modified" },
	{ "dateCreated",    "date_created" },
	{ "dateAdded",      "date_added" },
	{ "dateLastOpened", "date_accessed" },
};

static const char *
map_column (const char *finder)
{
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (column_map); i++) {
		if (g_strcmp0 (column_map[i].finder, finder) == 0)
			return column_map[i].fynder;
	}
	return NULL;
}

typedef struct {
	char *finder;
	gint64 index;
	gboolean visible;
	gboolean ascending;
} FinderColumn;

static gint
compare_columns (gconstpointer a, gconstpointer b)
{
	const FinderColumn *ca = *(FinderColumn * const *) a;
	const FinderColumn *cb = *(FinderColumn * const *) b;

	return (ca->index > cb->index) - (ca->index < cb->index);
}

static void
free_column (gpointer data)
{
	FinderColumn *c = data;

	g_free (c->finder);
	g_free (c);
}

/* Ajustes de la lista (lsvp): columnas visibles y su orden, columna y sentido de orden. */
static void
import_list_view (NemoFile *dir, GVariant *lsvp)
{
	GVariant *columns = g_variant_lookup_value (lsvp, "columns", G_VARIANT_TYPE ("a{sv}"));
	GPtrArray *cols = g_ptr_array_new_with_free_func (free_column);
	const char *sort_column = NULL;
	GList *visible = NULL, *order = NULL;
	guint i;

	if (columns != NULL) {
		GVariantIter iter;
		const char *name;
		GVariant *value;

		g_variant_iter_init (&iter, columns);
		while (g_variant_iter_next (&iter, "{&sv}", &name, &value)) {
			FinderColumn *c = g_new0 (FinderColumn, 1);

			c->finder = g_strdup (name);
			c->index = G_MAXINT64;
			g_variant_lookup (value, "index", "x", &c->index);
			g_variant_lookup (value, "visible", "b", &c->visible);
			g_variant_lookup (value, "ascending", "b", &c->ascending);
			g_ptr_array_add (cols, c);
			g_variant_unref (value);
		}
		g_variant_unref (columns);
	}
	g_ptr_array_sort (cols, compare_columns);

	for (i = 0; i < cols->len; i++) {
		FinderColumn *c = cols->pdata[i];
		const char *mine = map_column (c->finder);

		if (mine == NULL)
			continue;
		order = g_list_append (order, g_strdup (mine));
		if (c->visible)
			visible = g_list_append (visible, g_strdup (mine));
	}

	if (visible != NULL) {
		nemo_file_set_metadata_list (dir, NEMO_METADATA_KEY_LIST_VIEW_VISIBLE_COLUMNS, visible);
		nemo_file_set_metadata_list (dir, NEMO_METADATA_KEY_LIST_VIEW_COLUMN_ORDER, order);
	}

	if (g_variant_lookup (lsvp, "sortColumn", "&s", &sort_column)) {
		const char *mine = map_column (sort_column);

		if (mine != NULL) {
			gboolean ascending = TRUE;

			for (i = 0; i < cols->len; i++) {
				FinderColumn *c = cols->pdata[i];

				if (g_strcmp0 (c->finder, sort_column) == 0)
					ascending = c->ascending;
			}
			nemo_file_set_metadata (dir, NEMO_METADATA_KEY_LIST_VIEW_SORT_COLUMN, "file_name", mine);
			nemo_file_set_metadata (dir, NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED, "false",
						ascending ? "false" : "true");
		}
	}

	g_list_free_full (visible, g_free);
	g_list_free_full (order, g_free);
	g_ptr_array_free (cols, TRUE);
}

static NemoZoomLevel
zoom_for_icon_size (double size)
{
	NemoZoomLevel best = NEMO_ZOOM_LEVEL_STANDARD, level;
	double best_diff = G_MAXDOUBLE;

	for (level = NEMO_ZOOM_LEVEL_SMALLEST; level <= NEMO_ZOOM_LEVEL_LARGEST; level++) {
		double diff = ABS ((double) nemo_get_icon_size_for_zoom_level (level) - size);

		if (diff < best_diff) {
			best_diff = diff;
			best = level;
		}
	}
	return best;
}

/* Ajustes de la vista de íconos (icvp) y posiciones de los íconos (Iloc). */
static void
import_icon_view (NemoFile *dir, NemoDSStore *store, GVariant *icvp)
{
	double icon_size = 64;
	const char *arrange = "none";
	GPtrArray *files;
	GFile *dir_location = nemo_file_get_location (dir);
	guint i;
	gboolean any_position = FALSE;
	char *zoom;

	if (icvp != NULL) {
		g_variant_lookup (icvp, "iconSize", "d", &icon_size);
		g_variant_lookup (icvp, "arrangeBy", "&s", &arrange);
	}

	zoom = g_strdup_printf ("%d", (int) zoom_for_icon_size (icon_size));
	nemo_file_set_metadata (dir, NEMO_METADATA_KEY_ICON_VIEW_ZOOM_LEVEL, "standard", zoom);
	g_free (zoom);

	/* posiciones: el Finder guarda el centro del ícono; Fynder, la esquina de arriba a la izquierda */
	files = nemo_ds_store_list_files (store);
	for (i = 0; i < files->len; i++) {
		const char *name = files->pdata[i];
		int x, y;

		if (nemo_ds_store_get_icon_location (store, name, &x, &y) && x >= 0 && y >= 0) {
			GFile *child = g_file_get_child (dir_location, name);
			NemoFile *child_file = nemo_file_get (child);

			nemo_file_set_position (child_file,
						MAX (0, x - (int) icon_size / 2),
						MAX (0, y - (int) icon_size / 2));
			nemo_file_unref (child_file);
			g_object_unref (child);
			any_position = TRUE;
		}
	}
	g_ptr_array_free (files, TRUE);
	g_object_unref (dir_location);

	if (g_strcmp0 (arrange, "none") == 0 && any_position) {
		/* orden «None»: cada ícono se queda donde está */
		nemo_file_set_metadata (dir, NEMO_METADATA_KEY_ICON_VIEW_AUTO_LAYOUT, "true", "false");
	}
}

static const char *
view_id_for_style (const char *vstl)
{
	if (g_strcmp0 (vstl, "icnv") == 0)
		return NEMO_ICON_VIEW_ID;
	if (g_strcmp0 (vstl, "Nlsv") == 0)
		return NEMO_LIST_VIEW_ID;
	if (g_strcmp0 (vstl, "clmv") == 0)
		return FM_COMPACT_VIEW_ID;
	if (g_strcmp0 (vstl, "glyv") == 0)
		return NEMO_GALLERY_VIEW_ID;
	return NULL;
}

/* Ajustes de la carpeta: los de su propio .DS_Store («.»), o si no están, los que
 * su carpeta de arriba guarda con el nombre de ésta. */
static gboolean
has_settings (NemoDSStore *store, const char *name)
{
	const char *codes[] = { "vstl", "lsvp", "icvp", NULL };
	int i;

	for (i = 0; codes[i] != NULL; i++) {
		GVariant *v = nemo_ds_store_lookup (store, name, codes[i]);

		if (v != NULL) {
			g_variant_unref (v);
			return TRUE;
		}
	}
	return FALSE;
}

void
nemo_ds_store_import (NemoFile *directory)
{
	GFile *location;
	char *path = NULL, *parent_path = NULL, *own_db = NULL, *parent_db = NULL, *base = NULL;
	NemoDSStore *store = NULL, *own = NULL, *parent = NULL;
	const char *entry = ".";
	char *marker;
	GVariant *vstl, *lsvp, *icvp;
	const char *view_id = NULL;

	if (!g_settings_get_boolean (nemo_preferences, "import-ds-store") ||
	    directory == NULL || !nemo_file_is_directory (directory) || !nemo_file_is_local (directory))
		return;

	/* una sola vez por carpeta, y nunca sobre lo que el usuario ya ajustó aquí */
	marker = nemo_file_get_metadata (directory, IMPORTED_KEY, NULL);
	if (marker != NULL) {
		g_free (marker);
		return;
	}
	nemo_file_set_metadata (directory, IMPORTED_KEY, NULL, "true");

	marker = nemo_file_get_metadata (directory, NEMO_METADATA_KEY_DEFAULT_VIEW, NULL);
	if (marker != NULL) {
		g_free (marker);
		return;
	}

	location = nemo_file_get_location (directory);
	path = g_file_get_path (location);
	g_object_unref (location);
	if (path == NULL)
		return;

	base = g_path_get_basename (path);
	parent_path = g_path_get_dirname (path);
	own_db = g_build_filename (path, ".DS_Store", NULL);
	parent_db = g_build_filename (parent_path, ".DS_Store", NULL);

	own = nemo_ds_store_open (own_db);
	if (own != NULL && has_settings (own, ".")) {
		store = own;
		entry = ".";
	} else {
		parent = nemo_ds_store_open (parent_db);
		if (parent != NULL && has_settings (parent, base)) {
			store = parent;
			entry = base;
		}
	}

	if (store != NULL) {
		vstl = nemo_ds_store_lookup (store, entry, "vstl");
		lsvp = nemo_ds_store_lookup_plist (store, entry, "lsvp");
		icvp = nemo_ds_store_lookup_plist (store, entry, "icvp");

		if (vstl != NULL && g_variant_is_of_type (vstl, G_VARIANT_TYPE_STRING))
			view_id = view_id_for_style (g_variant_get_string (vstl, NULL));
		else if (lsvp != NULL && icvp == NULL)
			view_id = NEMO_LIST_VIEW_ID;
		else if (icvp != NULL && lsvp == NULL)
			view_id = NEMO_ICON_VIEW_ID;

		if (view_id != NULL)
			nemo_file_set_metadata (directory, NEMO_METADATA_KEY_DEFAULT_VIEW, NULL, view_id);
		if (lsvp != NULL)
			import_list_view (directory, lsvp);
		/* las posiciones están en el .DS_Store de la propia carpeta */
		if (own != NULL && (icvp != NULL || g_strcmp0 (view_id, NEMO_ICON_VIEW_ID) == 0 || !has_settings (own, ".")))
			import_icon_view (directory, own, icvp);

		g_clear_pointer (&vstl, g_variant_unref);
		g_clear_pointer (&lsvp, g_variant_unref);
		g_clear_pointer (&icvp, g_variant_unref);
	} else if (own != NULL) {
		/* sin ajustes de vista pero con posiciones de íconos (carpeta de íconos a mano) */
		import_icon_view (directory, own, NULL);
	}

	nemo_ds_store_free (own);
	nemo_ds_store_free (parent);
	g_free (path);
	g_free (parent_path);
	g_free (own_db);
	g_free (parent_db);
	g_free (base);
}
