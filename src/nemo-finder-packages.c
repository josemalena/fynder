/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * «Show Package Contents» y paquetes de Mac en zip.
 */

#include <config.h>

#include "nemo-finder-packages.h"

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <string.h>

static const char *package_extensions[] = {
	".app", ".bundle", ".framework", ".plugin", ".kext", ".prefpane", ".saver",
	".qlgenerator", ".mdimporter", ".xpc", ".appex", ".docset", ".wdgt", ".rtfd",
	".playground", ".xcarchive", ".xcodeproj", ".xcworkspace", ".xcassets",
	".pages", ".key", ".numbers", ".photoslibrary", ".imovielibrary", ".fcpbundle",
	".logicx", ".band", ".vmwarevm", ".pvm", ".sparsebundle", ".theme", NULL
};

static gboolean
has_package_extension (const char *name)
{
	gsize len = strlen (name);

	for (guint i = 0; package_extensions[i] != NULL; i++) {
		gsize elen = strlen (package_extensions[i]);

		if (len > elen && g_ascii_strcasecmp (name + len - elen, package_extensions[i]) == 0)
			return TRUE;
	}
	return FALSE;
}

static gboolean
file_has_zip_magic (NemoFile *file)
{
	char *path = nemo_file_is_local (file) ? nemo_file_get_path (file) : NULL;
	guchar magic[4];
	gboolean zip = FALSE;
	FILE *fp;

	if (path == NULL)
		return FALSE;
	fp = fopen (path, "rb");
	g_free (path);
	if (fp == NULL)
		return FALSE;
	if (fread (magic, 1, 4, fp) == 4 && magic[0] == 'P' && magic[1] == 'K' &&
	    ((magic[2] == 3 && magic[3] == 4) || (magic[2] == 5 && magic[3] == 6) ||
	     (magic[2] == 7 && magic[3] == 8)))
		zip = TRUE;
	fclose (fp);
	return zip;
}

NemoPackageKind
nemo_finder_package_kind (NemoFile *file)
{
	char *name;
	gboolean package;

	if (file == NULL)
		return NEMO_PACKAGE_NONE;
	name = nemo_file_get_name (file);
	package = name != NULL && has_package_extension (name);
	g_free (name);
	if (!package)
		return NEMO_PACKAGE_NONE;

	if (nemo_file_is_directory (file))
		return NEMO_PACKAGE_DIRECTORY;

	if (file_has_zip_magic (file))
		return NEMO_PACKAGE_ZIP;
	return NEMO_PACKAGE_NONE;
}

gboolean
nemo_finder_package_blocks_activation (NemoFile *file)
{
	static const char *archivers[] = {
		"file-roller", "engrampa", "ark", "xarchiver", "peazip", "org.gnome.FileRoller", NULL
	};
	char *content_type;
	GAppInfo *app;
	gboolean block = FALSE;

	if (nemo_finder_package_kind (file) != NEMO_PACKAGE_ZIP)
		return FALSE;

	content_type = nemo_file_get_mime_type (file);
	app = content_type != NULL ? g_app_info_get_default_for_type (content_type, FALSE) : NULL;
	if (app == NULL) {
		/* sin aplicación para ese tipo, el zip caería en el gestor de archivos comprimidos */
		block = TRUE;
	} else {
		const char *exe = g_app_info_get_executable (app);
		const char *id = g_app_info_get_id (app);
		char *base = exe != NULL ? g_path_get_basename (exe) : NULL;

		for (guint i = 0; archivers[i] != NULL; i++) {
			if ((base != NULL && strcmp (base, archivers[i]) == 0) ||
			    (id != NULL && g_str_has_prefix (id, archivers[i])))
				block = TRUE;
		}
		g_free (base);
		g_object_unref (app);
	}
	g_free (content_type);
	return block;
}

/* ---------- Show Package Contents ---------- */

typedef struct {
	NemoWindow *window;
	GFile      *root;
	char       *package_name;
} ShowData;

static void
show_data_free (ShowData *data)
{
	g_clear_object (&data->window);
	g_object_unref (data->root);
	g_free (data->package_name);
	g_free (data);
}

/* La raíz del zip suele traer el paquete con su nombre (MacPass.app/Contents/…):
 * se entra directo a esa carpeta. */
static GFile *
pick_target (ShowData *data)
{
	GFileEnumerator *en = g_file_enumerate_children (data->root,
							 G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
							 G_FILE_QUERY_INFO_NONE, NULL, NULL);
	GFile *only_dir = NULL, *named = NULL;
	guint count = 0;
	GFileInfo *info;

	if (en == NULL)
		return g_object_ref (data->root);

	while ((info = g_file_enumerator_next_file (en, NULL, NULL)) != NULL) {
		const char *name = g_file_info_get_name (info);

		count++;
		if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
			if (only_dir == NULL)
				only_dir = g_file_get_child (data->root, name);
			if (named == NULL && g_strcmp0 (name, data->package_name) == 0)
				named = g_file_get_child (data->root, name);
		}
		g_object_unref (info);
	}
	g_object_unref (en);

	if (named != NULL) {
		g_clear_object (&only_dir);
		return named;
	}
	if (only_dir != NULL && count == 1)
		return only_dir;
	g_clear_object (&only_dir);
	return g_object_ref (data->root);
}

static void
mount_done_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	ShowData *data = user_data;
	GError *error = NULL;

	if (!g_file_mount_enclosing_volume_finish (G_FILE (source), res, &error) &&
	    !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED)) {
		g_warning ("No se pudo abrir el paquete: %s", error->message);
		g_clear_error (&error);
		show_data_free (data);
		return;
	}
	g_clear_error (&error);

	if (data->window != NULL) {
		GFile *target = pick_target (data);

		nemo_window_go_to (data->window, target);
		g_object_unref (target);
	}
	show_data_free (data);
}

void
nemo_finder_show_package_contents (NemoFile *file, NemoWindow *window)
{
	NemoPackageKind kind = nemo_finder_package_kind (file);
	GFile *location = nemo_file_get_location (file);

	if (kind == NEMO_PACKAGE_DIRECTORY) {
		nemo_window_go_to (window, location);
	} else if (kind == NEMO_PACKAGE_ZIP) {
		char *uri = g_file_get_uri (location);
		char *once = g_uri_escape_string (uri, NULL, FALSE);
		char *twice = g_uri_escape_string (once, NULL, FALSE);
		char *archive_uri = g_strconcat ("archive://", twice, "/", NULL);
		ShowData *data = g_new0 (ShowData, 1);

		data->window = g_object_ref (window);
		data->root = g_file_new_for_uri (archive_uri);
		data->package_name = nemo_file_get_name (file);
		g_file_mount_enclosing_volume (data->root, G_MOUNT_MOUNT_NONE, NULL, NULL,
					       mount_done_cb, data);
		g_free (uri);
		g_free (once);
		g_free (twice);
		g_free (archive_uri);
	}
	g_object_unref (location);
}
