/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Acciones del menú contextual que el Finder trae y Nemo no: Compress, Burn
 * to Disc y Add to Dock. Cada una existe solo si la herramienta que la hace
 * está instalada (file-roller, xfburn o k3b, y Plank).
 */

#ifndef NEMO_FINDER_FILE_ACTIONS_H
#define NEMO_FINDER_FILE_ACTIONS_H

#include <gtk/gtk.h>
#include <libnemo-private/nemo-file.h>

gboolean nemo_finder_can_compress (void);
gboolean nemo_finder_can_burn     (void);
gboolean nemo_finder_can_add_to_dock (void);

/* files: lista de NemoFile, todos locales */
void nemo_finder_compress_files (GList *files);
void nemo_finder_burn_files     (GList *files);
/* file: una carpeta local */
void nemo_finder_add_to_dock    (NemoFile *file);

#endif /* NEMO_FINDER_FILE_ACTIONS_H */
