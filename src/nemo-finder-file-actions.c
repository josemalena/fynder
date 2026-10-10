/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Compress, Burn to Disc y Add to Dock.
 */

#include <config.h>

#include "nemo-finder-file-actions.h"

#include <gio/gio.h>
#include <glib/gi18n.h>

#define PLANK_SCHEMA "net.launchpad.plank.dock.settings"
#define PLANK_DOCK_PATH "/net/launchpad/plank/docks/dock1/"

/* ---------- herramientas disponibles ---------- */

static char *
burn_program (void)
{
	char *program = g_find_program_in_path ("xfburn");

	if (program == NULL)
		program = g_find_program_in_path ("k3b");
	return program;
}

gboolean
nemo_finder_can_compress (void)
{
	char *program = g_find_program_in_path ("file-roller");
	gboolean found = program != NULL;

	g_free (program);
	return found;
}

gboolean
nemo_finder_can_burn (void)
{
	char *program = burn_program ();
	gboolean found = program != NULL;

	g_free (program);
	return found;
}

gboolean
nemo_finder_can_add_to_dock (void)
{
	GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
	GSettingsSchema *schema;
	char *program;
	gboolean found;

	if (source == NULL)
		return FALSE;

	schema = g_settings_schema_source_lookup (source, PLANK_SCHEMA, TRUE);
	if (schema == NULL)
		return FALSE;
	g_settings_schema_unref (schema);

	program = g_find_program_in_path ("plank");
	found = program != NULL;
	g_free (program);
	return found;
}

/* ---------- utilidades ---------- */

static char *
local_path (NemoFile *file)
{
	char *uri = nemo_file_get_uri (file);
	char *path = g_filename_from_uri (uri, NULL, NULL);

	g_free (uri);
	return path;
}

static void
spawn_detached (char **argv)
{
	GError *error = NULL;

	if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
	                    G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &error)) {
		g_warning ("No se pudo lanzar %s: %s", argv[0], error->message);
		g_error_free (error);
	}
}

/* ---------- Compress ---------- */

/* El archivo queda junto al primero: «nombre.zip» (o «Archive.zip» si son
 * varios), con «nombre 2.zip», «nombre 3.zip»… si ya existe. */
static char *
unique_archive_path (const char *dir, const char *base)
{
	char *path = g_strdup_printf ("%s/%s.zip", dir, base);
	int n = 2;

	while (g_file_test (path, G_FILE_TEST_EXISTS)) {
		g_free (path);
		path = g_strdup_printf ("%s/%s %d.zip", dir, base, n++);
	}
	return path;
}

void
nemo_finder_compress_files (GList *files)
{
	GPtrArray *argv = g_ptr_array_new_with_free_func (g_free);
	char *first_path, *dir, *base, *archive;
	GList *l;

	if (files == NULL)
		return;

	first_path = local_path (NEMO_FILE (files->data));
	if (first_path == NULL) {
		g_ptr_array_free (argv, TRUE);
		return;
	}

	dir = g_path_get_dirname (first_path);
	if (files->next == NULL)
		base = g_path_get_basename (first_path);
	else
		base = g_strdup (_("Archive"));
	archive = unique_archive_path (dir, base);

	g_ptr_array_add (argv, g_strdup ("file-roller"));
	g_ptr_array_add (argv, g_strdup_printf ("--add-to=%s", archive));
	for (l = files; l != NULL; l = l->next) {
		char *path = local_path (NEMO_FILE (l->data));

		if (path != NULL)
			g_ptr_array_add (argv, path);
	}
	g_ptr_array_add (argv, NULL);

	spawn_detached ((char **) argv->pdata);

	g_ptr_array_free (argv, TRUE);
	g_free (first_path);
	g_free (dir);
	g_free (base);
	g_free (archive);
}

/* ---------- Burn to Disc ---------- */

void
nemo_finder_burn_files (GList *files)
{
	char *program = burn_program ();
	GPtrArray *argv;
	GList *l;

	if (program == NULL)
		return;

	argv = g_ptr_array_new_with_free_func (g_free);
	g_ptr_array_add (argv, program);
	/* los dos reciben «composición de datos» + archivos */
	g_ptr_array_add (argv, g_strdup (g_str_has_suffix (program, "k3b") ? "--data" : "--data-composition"));
	for (l = files; l != NULL; l = l->next) {
		char *path = local_path (NEMO_FILE (l->data));

		if (path != NULL)
			g_ptr_array_add (argv, path);
	}
	g_ptr_array_add (argv, NULL);

	spawn_detached ((char **) argv->pdata);
	g_ptr_array_free (argv, TRUE);
}

/* ---------- Add to Dock ---------- */

void
nemo_finder_add_to_dock (NemoFile *file)
{
	GSettings *settings;
	char *path, *uri, *name, *item_name, *item_path, *contents, *launchers_dir;
	char **items;
	GPtrArray *new_items;
	gint i;
	gboolean inserted = FALSE;

	path = local_path (file);
	if (path == NULL)
		return;

	uri = g_filename_to_uri (path, NULL, NULL);
	name = g_path_get_basename (path);

	/* un nombre de archivo de lanzador estable por carpeta */
	{
		char *checksum = g_compute_checksum_for_string (G_CHECKSUM_SHA1, path, -1);

		item_name = g_strdup_printf ("folder-%.8s.dockitem", checksum);
		g_free (checksum);
	}

	launchers_dir = g_build_filename (g_get_user_config_dir (), "plank", "dock1", "launchers", NULL);
	g_mkdir_with_parents (launchers_dir, 0755);
	item_path = g_build_filename (launchers_dir, item_name, NULL);

	contents = g_strdup_printf ("[PlankDockItemPreferences]\nLauncher=%s\n", uri);
	g_file_set_contents (item_path, contents, -1, NULL);

	/* se añade a la lista del dock, antes de la papelera (o al final) */
	settings = g_settings_new_with_path (PLANK_SCHEMA, PLANK_DOCK_PATH);
	items = g_settings_get_strv (settings, "dock-items");
	new_items = g_ptr_array_new ();

	for (i = 0; items[i] != NULL; i++) {
		if (g_strcmp0 (items[i], item_name) == 0) {
			inserted = TRUE; /* ya estaba */
		} else if (!inserted && g_strcmp0 (items[i], "trash.dockitem") == 0) {
			g_ptr_array_add (new_items, item_name);
			inserted = TRUE;
		}
		g_ptr_array_add (new_items, items[i]);
	}
	if (!inserted)
		g_ptr_array_add (new_items, item_name);
	g_ptr_array_add (new_items, NULL);

	g_settings_set_strv (settings, "dock-items", (const gchar * const *) new_items->pdata);
	g_settings_sync ();

	g_ptr_array_free (new_items, TRUE);
	g_strfreev (items);
	g_object_unref (settings);
	g_free (contents);
	g_free (item_path);
	g_free (launchers_dir);
	g_free (item_name);
	g_free (name);
	g_free (uri);
	g_free (path);
}
