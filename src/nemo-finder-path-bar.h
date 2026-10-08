/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Path Bar estilo Finder: segmentos con icono y nombre separados por «›»,
 * sin botones; si no caben se recortan los del medio con «…».
 */

#ifndef NEMO_FINDER_PATH_BAR_H
#define NEMO_FINDER_PATH_BAR_H

#include <gtk/gtk.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define NEMO_TYPE_FINDER_PATH_BAR (nemo_finder_path_bar_get_type ())

G_DECLARE_FINAL_TYPE (NemoFinderPathBar, nemo_finder_path_bar, NEMO, FINDER_PATH_BAR, GtkContainer)

GtkWidget *nemo_finder_path_bar_new          (void);
void       nemo_finder_path_bar_set_location (NemoFinderPathBar *bar,
                                              GFile             *location);

/* Señal «location-clicked» (GFile *): se hizo clic en un segmento. */

G_END_DECLS

#endif /* NEMO_FINDER_PATH_BAR_H */
