/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Atajos de teclado estilo Finder (Command = Super) para Nemo Mac.
 */

#ifndef NEMO_FINDER_SHORTCUTS_H
#define NEMO_FINDER_SHORTCUTS_H

#include <gtk/gtk.h>
#include "nemo-window.h"

/* TRUE si el evento era un atajo Finder y ya se atendió. */
gboolean nemo_finder_shortcuts_handle (NemoWindow *window,
                                       GdkEventKey *event);

#endif /* NEMO_FINDER_SHORTCUTS_H */
