/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Menú global: publica la barra de menú de Fynder (File, Edit, View, Go,
 * Bookmarks, Help) por el protocolo estándar de GTK (GMenuModel y
 * GActionGroup sobre D-Bus), para que un panel como el de Cinnamon la
 * muestre arriba, como en macOS.
 */

#ifndef NEMO_GLOBAL_MENU_H
#define NEMO_GLOBAL_MENU_H

#include <gtk/gtk.h>
#include "nemo-window.h"

/* Engancha la ventana al menú global. Lo que haga depende de la clave
 * «global-menu» y reacciona a sus cambios. */
void nemo_global_menu_install (NemoWindow *window);

/* TRUE si la barra de menú propia de la ventana debe quedar oculta. */
gboolean nemo_global_menu_hides_menubar (void);

#endif /* NEMO_GLOBAL_MENU_H */
