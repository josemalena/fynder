/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Atajos de teclado estilo Finder (Command = Super) para Fynder.
 */

#ifndef NEMO_FINDER_SHORTCUTS_H
#define NEMO_FINDER_SHORTCUTS_H

#include <gtk/gtk.h>
#include "nemo-window.h"

/* TRUE si el evento era un atajo Finder y ya se atendió. */
gboolean nemo_finder_shortcuts_handle (NemoWindow *window,
                                       GdkEventKey *event);

/* Hace que los menús muestren (y atiendan) los atajos Finder en vez de los de Nemo. */
void nemo_finder_shortcuts_install (NemoWindow *window);

/* Cmd + clic suma o quita de la selección, como en macOS: keyd entrega ⌘ como Super
 * en los clics, y GTK solo reconoce Ctrl para extender la selección. */
void nemo_finder_shortcuts_install_event_filter (void);

#endif /* NEMO_FINDER_SHORTCUTS_H */
