/* Paquetes de Mac (.app, .pages, .photoslibrary…) que iCloud entrega como zip.
 *
 * En la Mac son carpetas; aquí pueden llegar como un archivo zip con la misma
 * extensión. Se tratan como paquetes: «Show Package Contents» muestra el
 * contenido (solo lectura, con el backend archive de gvfs) y el doble clic no
 * abre el gestor de archivos comprimidos. */

#ifndef NEMO_FINDER_PACKAGES_H
#define NEMO_FINDER_PACKAGES_H

#include <gtk/gtk.h>
#include <libnemo-private/nemo-file.h>
#include "nemo-window.h"

typedef enum {
	NEMO_PACKAGE_NONE,
	NEMO_PACKAGE_DIRECTORY, /* carpeta de verdad con extensión de paquete */
	NEMO_PACKAGE_ZIP        /* archivo zip con extensión de paquete */
} NemoPackageKind;

NemoPackageKind nemo_finder_package_kind (NemoFile *file);

/* TRUE si doble clic debe ignorarse: es un zip de paquete y su aplicación
 * predeterminada es un gestor de archivos comprimidos. */
gboolean nemo_finder_package_blocks_activation (NemoFile *file);

void nemo_finder_show_package_contents (NemoFile *file, NemoWindow *window);

#endif
