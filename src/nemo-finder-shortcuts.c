/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Atajos de teclado estilo Finder (Command = Super) para Fynder.
 *
 * Se atienden desde nemo_window_key_press_event, después de que la ventana
 * haya dejado pasar los campos de texto y el renombrado en curso. Los atajos
 * de Nemo (Ctrl+...) siguen funcionando igual; estos se suman.
 */

#include <config.h>

#include "nemo-finder-shortcuts.h"

#include "nemo-actions.h"
#include "nemo-toolbar.h"
#include "nemo-view.h"
#include "nemo-window-pane.h"
#include "nemo-window-private.h"
#include "nemo-window-slot.h"

#include <gdk/gdkkeysyms.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>

typedef struct {
	guint keyval;
	GdkModifierType mods;
	const gchar *action;
	gboolean window_level; /* también válido escribiendo en un campo de texto */
} FinderShortcut;

#define CMD   GDK_SUPER_MASK
#define SHIFT GDK_SHIFT_MASK
#define ALT   GDK_MOD1_MASK

static const FinderShortcut shortcuts[] = {
	/* Cmd+Delete / Cmd+Backspace: a la papelera; con Shift, vaciarla */
	{ GDK_KEY_BackSpace,    CMD,         NEMO_ACTION_TRASH, FALSE },
	{ GDK_KEY_Delete,       CMD,         NEMO_ACTION_TRASH, FALSE },
	{ GDK_KEY_BackSpace,    CMD | SHIFT, NEMO_ACTION_EMPTY_TRASH, FALSE },
	{ GDK_KEY_Delete,       CMD | SHIFT, NEMO_ACTION_EMPTY_TRASH, FALSE },

	/* Quick Look */
	{ GDK_KEY_y,            CMD,         "Quick Look", FALSE },

	/* Abrir, propiedades (Get Info), carpeta nueva */
	{ GDK_KEY_o,            CMD,         NEMO_ACTION_OPEN, FALSE },
	{ GDK_KEY_Down,         CMD,         NEMO_ACTION_OPEN, FALSE },
	{ GDK_KEY_i,            CMD,         NEMO_ACTION_PROPERTIES, FALSE },
	{ GDK_KEY_n,            CMD | SHIFT, NEMO_ACTION_NEW_FOLDER, FALSE },

	/* Ventanas y pestañas */
	{ GDK_KEY_n,            CMD,         "New Window", TRUE },
	{ GDK_KEY_t,            CMD,         NEMO_ACTION_NEW_TAB, TRUE },
	{ GDK_KEY_w,            CMD,         NEMO_ACTION_CLOSE, TRUE },

	/* Navegación */
	{ GDK_KEY_bracketleft,  CMD,         NEMO_ACTION_BACK, TRUE },
	{ GDK_KEY_bracketright, CMD,         NEMO_ACTION_FORWARD, TRUE },
	{ GDK_KEY_Up,           CMD,         NEMO_ACTION_UP, TRUE },
	{ GDK_KEY_h,            CMD | SHIFT, NEMO_ACTION_HOME, TRUE },

	/* Vistas: iconos, lista, columnas (la compacta) */
	{ GDK_KEY_1,            CMD,         NEMO_ACTION_ICON_VIEW, TRUE },
	{ GDK_KEY_2,            CMD,         NEMO_ACTION_LIST_VIEW, TRUE },
	{ GDK_KEY_3,            CMD,         NEMO_ACTION_COMPACT_VIEW, TRUE },

	/* Ir a: equipo, red, servidor */
	{ GDK_KEY_c,            CMD | SHIFT, "Go to Computer", TRUE },
	{ GDK_KEY_k,            CMD | SHIFT, "Go to Network", TRUE },
	{ GDK_KEY_k,            CMD,         "Connect to Server", TRUE },

	/* Tamaño de los iconos (Cmd + / Cmd -; con teclado es, + sale con Shift) */
	{ GDK_KEY_plus,         CMD,         NEMO_ACTION_ZOOM_IN, TRUE },
	{ GDK_KEY_plus,         CMD | SHIFT, NEMO_ACTION_ZOOM_IN, TRUE },
	{ GDK_KEY_equal,        CMD,         NEMO_ACTION_ZOOM_IN, TRUE },
	{ GDK_KEY_minus,        CMD,         NEMO_ACTION_ZOOM_OUT, TRUE },
	{ GDK_KEY_0,            CMD,         NEMO_ACTION_ZOOM_NORMAL, TRUE },

	/* Mostrar u ocultar: lateral, barra de herramientas, ruta, barra de estado.
	 * El Finder usa Ctrl+Cmd+S para el lateral, pero Ctrl se funde con Cmd aquí. */
	{ GDK_KEY_s,            CMD | ALT,   NEMO_ACTION_SHOW_HIDE_SIDEBAR, TRUE },
	{ GDK_KEY_t,            CMD | ALT,   NEMO_ACTION_SHOW_HIDE_TOOLBAR, TRUE },
	{ GDK_KEY_p,            CMD | ALT,   NEMO_ACTION_SHOW_HIDE_PATH_BAR, TRUE },
	{ GDK_KEY_slash,        CMD,         NEMO_ACTION_SHOW_HIDE_STATUSBAR, TRUE },

	/* Alias (enlace simbólico) y expulsar */
	{ GDK_KEY_l,            CMD,         NEMO_ACTION_CREATE_LINK, FALSE },
	{ GDK_KEY_e,            CMD,         NEMO_ACTION_EJECT_VOLUME, FALSE },

	/* Edición */
	{ GDK_KEY_d,            CMD,         NEMO_ACTION_DUPLICATE, FALSE },
	{ GDK_KEY_c,            CMD,         NEMO_ACTION_COPY, FALSE },
	{ GDK_KEY_x,            CMD,         NEMO_ACTION_CUT, FALSE },
	{ GDK_KEY_v,            CMD,         NEMO_ACTION_PASTE, FALSE },
	{ GDK_KEY_a,            CMD,         NEMO_ACTION_SELECT_ALL, FALSE },

	/* Archivos ocultos (Cmd+Shift+. ; con teclado es sale como «:») */
	{ GDK_KEY_period,       CMD | SHIFT, NEMO_ACTION_SHOW_HIDDEN_FILES, FALSE },
	{ GDK_KEY_colon,        CMD | SHIFT, NEMO_ACTION_SHOW_HIDDEN_FILES, FALSE },

	/* Preferencias */
	{ GDK_KEY_comma,        CMD,         "Preferences", TRUE },
};

static GtkAction *
find_action (NemoWindow *window,
             const gchar *name)
{
	const GList *groups;
	NemoWindowPane *pane;

	for (groups = gtk_ui_manager_get_action_groups (nemo_window_get_ui_manager (window));
	     groups != NULL;
	     groups = groups->next) {
		GtkAction *action = gtk_action_group_get_action (groups->data, name);

		if (action != NULL)
			return action;
	}

	pane = nemo_window_get_active_pane (window);
	if (pane != NULL && pane->action_group != NULL)
		return gtk_action_group_get_action (pane->action_group, name);

	return NULL;
}

static gboolean
activate_action (NemoWindow *window,
                 const gchar *name)
{
	GtkAction *action = find_action (window, name);

	if (action == NULL || !gtk_action_is_sensitive (action))
		return FALSE;

	gtk_action_activate (action);
	return TRUE;
}

static void
go_to_folder (NemoWindow *window,
              GUserDirectory directory,
              const gchar *fallback)
{
	const gchar *path = g_get_user_special_dir (directory);
	gchar *fallback_path = NULL;
	GFile *location;

	/* user-dirs.dirs puede apuntar la carpeta a $HOME: usar ~/<nombre estándar> */
	if (path == NULL || g_strcmp0 (path, g_get_home_dir ()) == 0) {
		fallback_path = g_build_filename (g_get_home_dir (), fallback, NULL);
		path = fallback_path;
	}

	location = g_file_new_for_path (path);
	nemo_window_go_to (window, location);

	g_object_unref (location);
	g_free (fallback_path);
}

static gboolean
focus_is_in_view (NemoWindow *window,
                  NemoView *view)
{
	GtkWidget *focus = gtk_window_get_focus (GTK_WINDOW (window));

	return view != NULL && focus != NULL &&
	       (focus == GTK_WIDGET (view) || gtk_widget_is_ancestor (focus, GTK_WIDGET (view)));
}

/* Atajos de ventana (no dependen de la selección ni del foco en la vista).
 * Se usan también cuando el foco está en un campo de texto. */
static gboolean
nemo_finder_shortcuts_window_level (NemoWindow *window,
                                    GdkModifierType mods,
                                    guint keyval)
{
	gsize i;

	/* Cmd+F: ir al campo de búsqueda */
	if (mods == CMD && keyval == GDK_KEY_f) {
		NemoWindowPane *pane = nemo_window_get_active_pane (window);

		if (pane != NULL && NEMO_IS_TOOLBAR (pane->tool_bar) &&
		    nemo_toolbar_activate_search (NEMO_TOOLBAR (pane->tool_bar)))
			return TRUE;
		return activate_action (window, NEMO_ACTION_SEARCH);
	}

	/* Cmd+Shift+G: ir a una carpeta (la entrada de ubicación) */
	if (mods == (CMD | SHIFT) && keyval == GDK_KEY_g) {
		nemo_window_show_location_entry (window);
		return TRUE;
	}

	/* Cmd+Shift+D / +O, Cmd+Option+L: Desktop, Documents, Downloads */
	if (mods == (CMD | SHIFT) && keyval == GDK_KEY_d) {
		go_to_folder (window, G_USER_DIRECTORY_DESKTOP, "Desktop");
		return TRUE;
	}
	if (mods == (CMD | SHIFT) && keyval == GDK_KEY_o) {
		go_to_folder (window, G_USER_DIRECTORY_DOCUMENTS, "Documents");
		return TRUE;
	}
	if (mods == (CMD | GDK_MOD1_MASK) && keyval == GDK_KEY_l) {
		go_to_folder (window, G_USER_DIRECTORY_DOWNLOAD, "Downloads");
		return TRUE;
	}

	/* Cmd+M: minimizar */
	if (mods == CMD && keyval == GDK_KEY_m) {
		gtk_window_iconify (GTK_WINDOW (window));
		return TRUE;
	}

	for (i = 0; i < G_N_ELEMENTS (shortcuts); i++) {
		if (shortcuts[i].window_level &&
		    shortcuts[i].keyval == keyval && shortcuts[i].mods == mods) {
			/* el atajo se consume aunque la acción no aplique ahora, para que la
			 * tecla no se cuele al widget con el foco */
			activate_action (window, shortcuts[i].action);
			return TRUE;
		}
	}

	return FALSE;
}

gboolean
nemo_finder_shortcuts_handle (NemoWindow *window,
                              GdkEventKey *event)
{
	NemoWindowSlot *slot;
	NemoView *view;
	GtkWidget *focus;
	GdkModifierType mods;
	guint keyval;
	gsize i;

	if (!g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHORTCUTS_FINDER_STYLE))
		return FALSE;

	focus = gtk_window_get_focus (GTK_WINDOW (window));
	mods = event->state & gtk_accelerator_get_default_mod_mask ();
	keyval = gdk_keyval_to_lower (event->keyval);

	/* Escribiendo en un campo de texto: sin Super no se toca nada. Con Super,
	 * GtkEntry insertaría la letra (Super no está en su filtro), así que se
	 * atienden solo los atajos de ventana y el portapapeles, y el resto se
	 * descarta. */
	if (focus != NULL && (GTK_IS_EDITABLE (focus) || GTK_IS_TEXT_VIEW (focus))) {
		if (!(mods & CMD))
			return FALSE;

		if (mods == CMD && GTK_IS_EDITABLE (focus)) {
			const gchar *signal_name = NULL;

			if (keyval == GDK_KEY_c)
				signal_name = "copy-clipboard";
			else if (keyval == GDK_KEY_x)
				signal_name = "cut-clipboard";
			else if (keyval == GDK_KEY_v)
				signal_name = "paste-clipboard";
			else if (keyval == GDK_KEY_a)
				signal_name = "select-all";

			if (signal_name != NULL) {
				if (g_strcmp0 (signal_name, "select-all") == 0)
					gtk_editable_select_region (GTK_EDITABLE (focus), 0, -1);
				else
					g_signal_emit_by_name (focus, signal_name);
				return TRUE;
			}
		}

		/* Delete y Backspace con Cmd los dejamos al campo */
		if (keyval == GDK_KEY_BackSpace || keyval == GDK_KEY_Delete)
			return FALSE;

		if (!nemo_finder_shortcuts_window_level (window, mods, keyval))
			return gdk_keyval_to_unicode (event->keyval) != 0;

		return TRUE;
	}

	/* ⌘ llega como Ctrl o como Super según la tecla: con keyd, ⌘+letra sale como
	 * Ctrl+letra y las teclas sin regla (⌘1…4, ⌘[ ], ⌘Espacio…) como Super. Fuera
	 * de los campos de texto se tratan igual. */
	if ((mods & (GDK_CONTROL_MASK | GDK_SUPER_MASK)) != 0)
		mods = (mods & ~(GDK_CONTROL_MASK | GDK_SUPER_MASK)) | CMD;

	slot = nemo_window_get_active_slot (window);
	view = (slot != NULL) ? slot->content_view : NULL;

	/* Return renombra, pero solo con el foco en la vista de archivos: en el
	 * resto de la ventana (lateral, barra de ubicación) conserva su función. */
	if (mods == 0 &&
	    (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)) {
		if (focus_is_in_view (window, view))
			return activate_action (window, NEMO_ACTION_RENAME);
		return FALSE;
	}

	if (nemo_finder_shortcuts_window_level (window, mods, keyval))
		return TRUE;

	for (i = 0; i < G_N_ELEMENTS (shortcuts); i++) {
		if (shortcuts[i].keyval == keyval && shortcuts[i].mods == mods) {
			activate_action (window, shortcuts[i].action);
			return TRUE;
		}
	}

	return FALSE;
}
