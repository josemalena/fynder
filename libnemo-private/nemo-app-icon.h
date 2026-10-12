/* Icono de un paquete de Mac (.app y similares): el .icns que trae dentro.
 *
 * Sirve con el paquete como carpeta y con el paquete como zip (iCloud). El PNG
 * del .icns se guarda en ~/.cache/fynder/app-icons/ y de ahí lo toma el icono. */

#ifndef NEMO_APP_ICON_H
#define NEMO_APP_ICON_H

#include <gio/gio.h>

/* TRUE si el nombre tiene extensión de paquete de Mac (.app, .pages, .photoslibrary…) */
gboolean nemo_app_bundle_has_extension (const char *name);

/* path: archivo local; mtime: para invalidar la caché. NULL si no tiene icono. */
GFile *nemo_app_bundle_icon_file (const char *path, gboolean is_directory, gint64 mtime);

#endif
