/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 * Nemo
 *
 * Copyright (C) 2011, Red Hat, Inc.
 *
 * Nemo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nemo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, MA 02110-1335, USA.
 *
 * Author: Cosimo Cecchi <cosimoc@redhat.com>
 *
 */

#include <config.h>

#include "nemo-toolbar.h"

#include "nemo-location-bar.h"
#include "nemo-pathbar.h"
#include "nemo-window-private.h"
#include "nemo-actions.h"
#include "nemo-file-utilities.h"
#include <glib/gi18n.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-ui-utilities.h>

struct _NemoToolbarPriv {
	GtkWidget *toolbar;

	GtkActionGroup *action_group;
	GtkUIManager *ui_manager;

    GtkWidget *previous_button;
    GtkWidget *next_button;
    GtkWidget *up_button;
    GtkWidget *refresh_button;
    GtkWidget *home_button;
    GtkWidget *computer_button;
    GtkWidget *toggle_location_button;
    GtkWidget *open_terminal_button;
    GtkWidget *new_folder_button;
    GtkWidget *search_button;
    GtkWidget *icon_view_button;
    GtkWidget *list_view_button;
    GtkWidget *compact_view_button;
    GtkWidget *view_switcher;
    GtkWidget *navigation_box;
    GtkWidget *show_thumbnails_button;
    GtkWidget *show_extra_pane_button;
    GtkWidget *arrange_button;
    GtkWidget *action_button;
    GtkWidget *action_menu;
    GtkWidget *get_info_button;
    GtkWidget *delete_button;
    GtkWidget *sidebar_button;
    GtkWidget *path_button;
    GtkWidget *search_item;
    GtkWidget *search_entry;
    gboolean updating_search;
    gboolean select_on_focus;

    GtkUIManager *window_ui_manager;

	GtkWidget *path_bar;
	GtkWidget *location_bar;
    GtkWidget *root_bar;
    GtkWidget *stack;

	gboolean show_main_bar;
	gboolean show_location_entry;
    gboolean show_root_bar;
};

enum {
	PROP_ACTION_GROUP = 1,
	PROP_SHOW_LOCATION_ENTRY,
	PROP_SHOW_MAIN_BAR,
	NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

enum {
    CHECK_ADMIN_LOCATION,
    SEARCH_TEXT_CHANGED,
    SEARCH_ESCAPED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (NemoToolbar, nemo_toolbar, GTK_TYPE_BOX);

static void
nemo_toolbar_update_root_state (NemoToolbar *self)
{
    gboolean is_admin_uri;

    g_signal_emit (self, signals[CHECK_ADMIN_LOCATION], 0, &is_admin_uri);

    if ((is_admin_uri ||
         (nemo_user_is_root () && !nemo_treating_root_as_normal())) &&
         g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_ROOT_WARNING)) {
        if (self->priv->show_root_bar != TRUE) {
            self->priv->show_root_bar = TRUE;
        }
    } else {
        self->priv->show_root_bar = FALSE;
    }
}

#define ARRANGE_ITEMS_PATH "/MenuBar/View/View Items Placeholder/Arrange Items"

static GtkWidget *
toolbar_create_menu_button (const gchar *icon_name,
                            const gchar *tooltip)
{
    GtkWidget *button = gtk_button_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);

    gtk_container_add (GTK_CONTAINER (box),
                       gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_container_add (GTK_CONTAINER (box),
                       gtk_image_new_from_icon_name ("pan-down-symbolic", GTK_ICON_SIZE_MENU));
    gtk_container_add (GTK_CONTAINER (button), box);
    gtk_widget_set_tooltip_text (button, tooltip);
    gtk_widget_set_can_focus (button, FALSE);
    gtk_style_context_add_class (gtk_widget_get_style_context (button), GTK_STYLE_CLASS_FLAT);
    gtk_widget_show_all (button);

    return button;
}

static void
menu_deactivated_restore_tooltip_cb (GtkMenuShell *menu,
                                     GtkWidget *button)
{
    gtk_widget_set_has_tooltip (button, TRUE);
    g_signal_handlers_disconnect_by_func (menu, menu_deactivated_restore_tooltip_cb, button);
}

/* El tooltip del botón se quedaba encima del primer ítem del menú abierto. */
static void
toolbar_popup_menu_at_button (GtkWidget *menu,
                              GtkWidget *button)
{
    gtk_widget_set_has_tooltip (button, FALSE);
    g_signal_connect (menu, "deactivate",
                      G_CALLBACK (menu_deactivated_restore_tooltip_cb), button);
    gtk_menu_popup_at_widget (GTK_MENU (menu), button,
                              GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

static GtkWidget *
toolbar_get_arrange_menu (NemoToolbar *self)
{
    GtkWidget *item;

    if (self->priv->window_ui_manager == NULL)
        return NULL;

    item = gtk_ui_manager_get_widget (self->priv->window_ui_manager, ARRANGE_ITEMS_PATH);
    if (item == NULL || !gtk_widget_get_visible (item))
        return NULL;

    return gtk_menu_item_get_submenu (GTK_MENU_ITEM (item));
}

/* Arrange solo existe cuando la vista activa registra «Arrange Items»
 * (iconos y compacta); en lista no hay menú de orden y el botón se oculta. */
static void
toolbar_update_arrange_button (NemoToolbar *self)
{
    gboolean finder_layout = g_settings_get_boolean (nemo_preferences,
                                                     NEMO_PREFERENCES_TOOLBAR_FINDER_LAYOUT);

    if (self->priv->arrange_button == NULL)
        return;

    gtk_widget_set_visible (self->priv->arrange_button,
                            finder_layout && toolbar_get_arrange_menu (self) != NULL);
}

static void
arrange_button_clicked_cb (GtkButton *button,
                           NemoToolbar *self)
{
    GtkWidget *menu = toolbar_get_arrange_menu (self);

    if (menu == NULL)
        return;

    toolbar_popup_menu_at_button (menu, GTK_WIDGET (button));
}

static GtkAction *
toolbar_find_action (NemoToolbar *self,
                     const gchar *name)
{
    GList *groups, *l;

    if (self->priv->window_ui_manager != NULL) {
        groups = gtk_ui_manager_get_action_groups (self->priv->window_ui_manager);
        for (l = groups; l != NULL; l = l->next) {
            GtkAction *action = gtk_action_group_get_action (GTK_ACTION_GROUP (l->data), name);

            if (action != NULL)
                return action;
        }
    }

    return gtk_action_group_get_action (self->priv->action_group, name);
}

/* Botones opcionales del layout Finder. Sus acciones viven en el grupo de la
 * vista activa, que cambia al cambiar de vista: se vuelven a enlazar cada vez. */
static GtkWidget *
toolbar_create_finder_button (const gchar *icon_name,
                              const gchar *tooltip,
                              gboolean toggle)
{
    GtkWidget *button = toggle ? gtk_toggle_button_new () : gtk_button_new ();
    GtkWidget *image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_SMALL_TOOLBAR);

    gtk_button_set_image (GTK_BUTTON (button), image);
    gtk_button_set_always_show_image (GTK_BUTTON (button), TRUE);
    gtk_activatable_set_use_action_appearance (GTK_ACTIVATABLE (button), FALSE);
    gtk_widget_set_tooltip_text (button, tooltip);
    gtk_widget_set_can_focus (button, FALSE);
    gtk_style_context_add_class (gtk_widget_get_style_context (button), GTK_STYLE_CLASS_FLAT);
    gtk_widget_show_all (button);

    return button;
}

static void
toolbar_bind_finder_button (NemoToolbar *self,
                            GtkWidget *button,
                            const gchar *action_name,
                            const gchar *pref_key,
                            gboolean finder_layout)
{
    GtkAction *action = toolbar_find_action (self, action_name);

    if (gtk_activatable_get_related_action (GTK_ACTIVATABLE (button)) != action)
        gtk_activatable_set_related_action (GTK_ACTIVATABLE (button), action);

    gtk_widget_set_visible (button,
                            finder_layout && action != NULL &&
                            g_settings_get_boolean (nemo_preferences, pref_key));
}

static void
toolbar_update_finder_buttons (NemoToolbar *self)
{
    gboolean finder_layout = g_settings_get_boolean (nemo_preferences,
                                                     NEMO_PREFERENCES_TOOLBAR_FINDER_LAYOUT);

    if (self->priv->get_info_button == NULL)
        return;

    toolbar_bind_finder_button (self, self->priv->get_info_button, NEMO_ACTION_PROPERTIES,
                                NEMO_PREFERENCES_TOOLBAR_FINDER_SHOW_GET_INFO, finder_layout);
    toolbar_bind_finder_button (self, self->priv->delete_button, NEMO_ACTION_TRASH,
                                NEMO_PREFERENCES_TOOLBAR_FINDER_SHOW_DELETE, finder_layout);
    toolbar_bind_finder_button (self, self->priv->sidebar_button, NEMO_ACTION_SHOW_HIDE_SIDEBAR,
                                NEMO_PREFERENCES_TOOLBAR_FINDER_SHOW_SIDEBAR, finder_layout);
    toolbar_bind_finder_button (self, self->priv->path_button, NEMO_ACTION_SHOW_HIDE_PATH_BAR,
                                NEMO_PREFERENCES_TOOLBAR_FINDER_SHOW_PATH, finder_layout);
}

/* Acciones del elemento seleccionado, en el orden del Action ▾ del Finder.
 * NULL separa grupos. Comprimir no está: en Nemo lo aportan extensiones. */
static const gchar *action_menu_entries[] = {
    NEMO_ACTION_NEW_FOLDER,
    NULL,
    NEMO_ACTION_OPEN,
    "OtherApplication2",
    NULL,
    NEMO_ACTION_RENAME,
    NEMO_ACTION_TRASH,
    NULL,
    NEMO_ACTION_PROPERTIES,
    NEMO_ACTION_SHOW_HIDDEN_FILES,
};

static void
action_button_clicked_cb (GtkButton *button,
                          NemoToolbar *self)
{
    gboolean need_separator = FALSE, has_items = FALSE;
    guint i;

    if (self->priv->action_menu != NULL) {
        gtk_widget_destroy (self->priv->action_menu);
        g_clear_object (&self->priv->action_menu);
    }

    /* Se arma en cada clic para reflejar la vista y la selección actuales. */
    self->priv->action_menu = gtk_menu_new ();
    g_object_ref_sink (self->priv->action_menu);

    for (i = 0; i < G_N_ELEMENTS (action_menu_entries); i++) {
        GtkAction *action;
        GtkWidget *item;

        if (action_menu_entries[i] == NULL) {
            need_separator = has_items;
            continue;
        }

        action = toolbar_find_action (self, action_menu_entries[i]);
        if (action == NULL || !gtk_action_get_visible (action))
            continue;

        if (need_separator) {
            gtk_menu_shell_append (GTK_MENU_SHELL (self->priv->action_menu),
                                   gtk_separator_menu_item_new ());
            need_separator = FALSE;
        }

        item = gtk_action_create_menu_item (action);
        gtk_menu_shell_append (GTK_MENU_SHELL (self->priv->action_menu), item);
        has_items = TRUE;
    }

    if (!has_items)
        return;

    gtk_widget_show_all (self->priv->action_menu);
    toolbar_popup_menu_at_button (self->priv->action_menu, GTK_WIDGET (button));
}

#define FINDER_SEARCH_WIDTH 220

/* Recuperar el foco (p. ej. al refrescarse los resultados) no debe dejar el
 * texto seleccionado: lo siguiente que teclees lo reemplazaría. */
static void
search_entry_grab_focus_cb (GtkWidget *entry,
                            NemoToolbar *self)
{
    if (!self->priv->select_on_focus)
        gtk_editable_set_position (GTK_EDITABLE (entry), -1);
}

static void
search_entry_stop_cb (GtkSearchEntry *entry,
                      NemoToolbar *self)
{
    /* Escape: vaciar el campo cierra la búsqueda (search-changed con texto vacío)
     * y el foco vuelve a la vista, aunque el campo ya estuviera vacío. */
    gtk_entry_set_text (GTK_ENTRY (entry), "");
    g_signal_emit (self, signals[SEARCH_ESCAPED], 0);
}

static void
search_entry_changed_cb (GtkSearchEntry *entry,
                         NemoToolbar *self)
{
    if (self->priv->updating_search)
        return;

    g_signal_emit (self, signals[SEARCH_TEXT_CHANGED], 0,
                   gtk_entry_get_text (GTK_ENTRY (entry)));
}

static void
toolbar_update_appearance (NemoToolbar *self)
{
	GtkWidget *widgetitem;
	gboolean icon_toolbar;
	gboolean show_location_entry;
    gboolean finder_layout;

    nemo_toolbar_update_root_state (self);

	show_location_entry = self->priv->show_location_entry;

	gtk_widget_set_visible (GTK_WIDGET(self->priv->toolbar),
				self->priv->show_main_bar);

    if (show_location_entry) {
        gtk_stack_set_visible_child_name (GTK_STACK (self->priv->stack), "location_bar");
    } else {
        gtk_stack_set_visible_child_name (GTK_STACK (self->priv->stack), "path_bar");
    }

    gtk_widget_set_visible (self->priv->root_bar,
                self->priv->show_root_bar);

        /* Please refer to the element name, not the action name after the forward slash, otherwise the prefs will not work*/

    widgetitem = self->priv->previous_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_PREVIOUS_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->next_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_NEXT_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->up_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_UP_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->refresh_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_RELOAD_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->home_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_HOME_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->computer_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_COMPUTER_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->search_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_SEARCH_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->new_folder_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_NEW_FOLDER_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->open_terminal_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_OPEN_IN_TERMINAL_TOOLBAR);
    if (icon_toolbar == FALSE ) {gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->toggle_location_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_EDIT_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->icon_view_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_ICON_VIEW_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->list_view_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_LIST_VIEW_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->compact_view_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_COMPACT_VIEW_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->show_thumbnails_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_SHOW_THUMBNAILS_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->show_extra_pane_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_TOGGLE_EXTRA_PANE_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    if (g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_TOOLBAR_LINKED_VIEW_SWITCHER)) {
        gtk_style_context_add_class (gtk_widget_get_style_context (self->priv->view_switcher), "linked");
    } else {
        gtk_style_context_remove_class (gtk_widget_get_style_context (self->priv->view_switcher), "linked");
    }

    finder_layout = g_settings_get_boolean (nemo_preferences,
                                             NEMO_PREFERENCES_TOOLBAR_FINDER_LAYOUT);
    if (finder_layout) {
        gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)), "nemo-finder-toolbar");
        gtk_style_context_add_class (gtk_widget_get_style_context (self->priv->toolbar), "nemo-finder-toolbar-inner");
        gtk_style_context_add_class (gtk_widget_get_style_context (self->priv->navigation_box), "linked");
        gtk_style_context_add_class (gtk_widget_get_style_context (self->priv->view_switcher), "linked");

        gtk_widget_show (self->priv->previous_button);
        gtk_widget_show (self->priv->next_button);
        gtk_widget_hide (self->priv->up_button);
        gtk_widget_hide (self->priv->refresh_button);
        gtk_widget_hide (self->priv->home_button);
        gtk_widget_hide (self->priv->computer_button);
        gtk_widget_hide (self->priv->toggle_location_button);
        /* el campo de búsqueda ya cubre la lupa */
        gtk_widget_hide (self->priv->search_button);
    } else {
        gtk_style_context_remove_class (gtk_widget_get_style_context (GTK_WIDGET (self)), "nemo-finder-toolbar");
        gtk_style_context_remove_class (gtk_widget_get_style_context (self->priv->toolbar), "nemo-finder-toolbar-inner");
        gtk_style_context_remove_class (gtk_widget_get_style_context (self->priv->navigation_box), "linked");
    }

    /* En el layout Finder la vista compacta lleva el glifo de columnas del Finder. */
    {
        GtkButton *compact = GTK_BUTTON (self->priv->compact_view_button);
        GtkActivatable *activatable = GTK_ACTIVATABLE (compact);

        gtk_activatable_set_use_action_appearance (activatable, !finder_layout);
        if (finder_layout) {
            gtk_button_set_image (compact,
                                  gtk_image_new_from_icon_name ("view-column-symbolic",
                                                                GTK_ICON_SIZE_SMALL_TOOLBAR));
            gtk_button_set_always_show_image (compact, TRUE);
        } else {
            gtk_activatable_sync_action_properties (activatable,
                                                    gtk_activatable_get_related_action (activatable));
        }
    }

    /* Con el layout Finder el breadcrumb no va en el toolbar: la ruta vive en la
     * Path Bar sobre la statusbar. Solo se muestra aquí la entrada de ubicación. */
    gtk_widget_set_visible (self->priv->stack,
                            !finder_layout || self->priv->show_location_entry);

    toolbar_update_arrange_button (self);
    gtk_widget_set_visible (self->priv->action_button, finder_layout);
    gtk_widget_set_visible (self->priv->search_item, finder_layout);
    toolbar_update_finder_buttons (self);
}

static void
setup_root_info_bar (NemoToolbar *self) {

    GtkWidget *root_bar = gtk_info_bar_new ();
    gtk_info_bar_set_message_type (GTK_INFO_BAR (root_bar), GTK_MESSAGE_ERROR);
    GtkWidget *content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (root_bar));

    GtkWidget *label = gtk_label_new (_("Elevated Privileges"));
    gtk_widget_show (label);
    gtk_container_add (GTK_CONTAINER (content_area), label);

    self->priv->root_bar = root_bar;
    gtk_box_pack_start (GTK_BOX (self), self->priv->root_bar, TRUE, TRUE, 0);
}

static GtkWidget *
toolbar_create_toolbutton (NemoToolbar *self,
                gboolean create_toggle,
                const gchar *name)
{
    GtkWidget *button;
    GtkWidget *image;
    GtkAction *action;

    if (create_toggle)
    {
        button = gtk_toggle_button_new ();
    } else {
        button = gtk_button_new ();
    }

    image = gtk_image_new ();

    gtk_button_set_image (GTK_BUTTON (button), image);
    action = gtk_action_group_get_action (self->priv->action_group, name);
    gtk_activatable_set_related_action (GTK_ACTIVATABLE (button), action);
    gtk_button_set_label (GTK_BUTTON (button), NULL);
    gtk_widget_set_tooltip_text (button, gtk_action_get_tooltip (action));
    gtk_widget_set_can_focus (button, FALSE);
    gtk_style_context_add_class (gtk_widget_get_style_context (button), GTK_STYLE_CLASS_FLAT);

    return button;
}

static void
nemo_toolbar_constructed (GObject *obj)
{
	NemoToolbar *self = NEMO_TOOLBAR (obj);
	GtkWidget *toolbar;
    GtkWidget *hbox;
    GtkToolItem *tool_box;
    GtkWidget *box;
	GtkStyleContext *context;

	G_OBJECT_CLASS (nemo_toolbar_parent_class)->constructed (obj);

	gtk_style_context_set_junction_sides (gtk_widget_get_style_context (GTK_WIDGET (self)),
					      GTK_JUNCTION_BOTTOM);

    self->priv->show_location_entry = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_LOCATION_ENTRY);

	/* add the UI */
	self->priv->ui_manager = gtk_ui_manager_new ();
	gtk_ui_manager_insert_action_group (self->priv->ui_manager, self->priv->action_group, 0);

	toolbar = gtk_toolbar_new ();
	self->priv->toolbar = toolbar;
    gtk_box_pack_start (GTK_BOX (self), self->priv->toolbar, TRUE, TRUE, 0);

	context = gtk_widget_get_style_context (GTK_WIDGET(toolbar));
	gtk_style_context_add_class (context, GTK_STYLE_CLASS_PRIMARY_TOOLBAR);

    /* Left side of the toolbar */
    tool_box = gtk_tool_item_new ();
    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
    self->priv->navigation_box = box;

    self->priv->previous_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_BACK);
    gtk_container_add (GTK_CONTAINER (box), self->priv->previous_button);

    self->priv->next_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_FORWARD);
    gtk_container_add (GTK_CONTAINER (box), self->priv->next_button);

    self->priv->up_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_UP);
    gtk_container_add (GTK_CONTAINER (box), self->priv->up_button);

    self->priv->refresh_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_RELOAD);
    gtk_container_add (GTK_CONTAINER (box), self->priv->refresh_button);

    self->priv->home_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_HOME);
    gtk_container_add (GTK_CONTAINER (box), self->priv->home_button);

    self->priv->computer_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_COMPUTER);
    gtk_container_add (GTK_CONTAINER (box), self->priv->computer_button);

    gtk_container_add (GTK_CONTAINER (tool_box), GTK_WIDGET (box));
    gtk_container_add (GTK_CONTAINER (self->priv->toolbar), GTK_WIDGET (tool_box));

    gtk_widget_show_all (GTK_WIDGET (tool_box));
    gtk_widget_set_margin_right (GTK_WIDGET (tool_box), 6);

    /* Container to hold the location and pathbars */
    self->priv->stack = gtk_stack_new();
    gtk_stack_set_transition_type (GTK_STACK (self->priv->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration (GTK_STACK (self->priv->stack), 150);

    /* Regular Path Bar */
    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (self->priv->stack), TRUE, TRUE, 0);

    self->priv->path_bar = g_object_new (NEMO_TYPE_PATH_BAR, NULL);
    gtk_stack_add_named(GTK_STACK (self->priv->stack), GTK_WIDGET (self->priv->path_bar), "path_bar");

    /* Entry-Like Location Bar */
    self->priv->location_bar = nemo_location_bar_new ();
    gtk_stack_add_named(GTK_STACK (self->priv->stack), GTK_WIDGET (self->priv->location_bar), "location_bar");
    gtk_widget_show_all (hbox);

    tool_box = gtk_tool_item_new ();
    gtk_tool_item_set_expand (tool_box, TRUE);
    gtk_container_add (GTK_CONTAINER (tool_box), hbox);
    gtk_container_add (GTK_CONTAINER (self->priv->toolbar), GTK_WIDGET (tool_box));
    gtk_widget_show (GTK_WIDGET (tool_box));

    /* Right Side of the toolbar */
    tool_box = gtk_tool_item_new ();
    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);

    self->priv->toggle_location_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_TOGGLE_LOCATION);
    gtk_container_add (GTK_CONTAINER (box), self->priv->toggle_location_button);

    self->priv->open_terminal_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_OPEN_IN_TERMINAL);
    gtk_container_add (GTK_CONTAINER (box), self->priv->open_terminal_button);

    self->priv->new_folder_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_NEW_FOLDER);
    gtk_container_add (GTK_CONTAINER (box), self->priv->new_folder_button);

    self->priv->search_button = toolbar_create_toolbutton (self, TRUE, NEMO_ACTION_SEARCH);
    gtk_container_add (GTK_CONTAINER (box), self->priv->search_button);

    self->priv->show_thumbnails_button = toolbar_create_toolbutton (self, TRUE, NEMO_ACTION_SHOW_THUMBNAILS);
    gtk_container_add (GTK_CONTAINER (box), self->priv->show_thumbnails_button);

    setup_root_info_bar (self);

    self->priv->show_extra_pane_button = toolbar_create_toolbutton (self, TRUE, NEMO_ACTION_SHOW_HIDE_EXTRA_PANE);
    gtk_container_add (GTK_CONTAINER (box), self->priv->show_extra_pane_button);

    self->priv->view_switcher = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

    self->priv->icon_view_button = toolbar_create_toolbutton (self, TRUE, NEMO_ACTION_ICON_VIEW);
    gtk_container_add (GTK_CONTAINER (self->priv->view_switcher), self->priv->icon_view_button);

    self->priv->list_view_button = toolbar_create_toolbutton (self, TRUE, NEMO_ACTION_LIST_VIEW);
    gtk_container_add (GTK_CONTAINER (self->priv->view_switcher), self->priv->list_view_button);

    self->priv->compact_view_button = toolbar_create_toolbutton (self, TRUE, NEMO_ACTION_COMPACT_VIEW);
    gtk_container_add (GTK_CONTAINER (self->priv->view_switcher), self->priv->compact_view_button);
    gtk_container_add (GTK_CONTAINER (box), self->priv->view_switcher);

    /* Solo en el layout Finder: menú de orden de la vista activa */
    self->priv->arrange_button = toolbar_create_menu_button ("view-sort-ascending-symbolic",
                                                             _("Arrange items"));
    g_signal_connect (self->priv->arrange_button, "clicked",
                      G_CALLBACK (arrange_button_clicked_cb), self);
    gtk_container_add (GTK_CONTAINER (box), self->priv->arrange_button);

    self->priv->action_button = toolbar_create_menu_button ("emblem-system-symbolic",
                                                            _("Actions for the selected items"));
    g_signal_connect (self->priv->action_button, "clicked",
                      G_CALLBACK (action_button_clicked_cb), self);
    gtk_container_add (GTK_CONTAINER (box), self->priv->action_button);

    self->priv->get_info_button = toolbar_create_finder_button ("document-properties-symbolic",
                                                                _("Get Info"), FALSE);
    gtk_container_add (GTK_CONTAINER (box), self->priv->get_info_button);

    self->priv->delete_button = toolbar_create_finder_button ("user-trash-symbolic",
                                                              _("Move to Trash"), FALSE);
    gtk_container_add (GTK_CONTAINER (box), self->priv->delete_button);

    self->priv->sidebar_button = toolbar_create_finder_button ("view-sidebar-symbolic",
                                                               _("Show or hide the sidebar"), TRUE);
    gtk_container_add (GTK_CONTAINER (box), self->priv->sidebar_button);

    self->priv->path_button = toolbar_create_finder_button ("view-path-symbolic",
                                                            _("Show or hide the path bar"), TRUE);
    gtk_container_add (GTK_CONTAINER (box), self->priv->path_button);

    gtk_container_add (GTK_CONTAINER (tool_box), GTK_WIDGET (box));
    gtk_container_add (GTK_CONTAINER (self->priv->toolbar), GTK_WIDGET (tool_box));

    gtk_widget_show_all (GTK_WIDGET (tool_box));
    gtk_widget_set_margin_left (GTK_WIDGET (tool_box), 6);

    /* Campo de búsqueda fijo a la derecha (solo layout Finder) */
    tool_box = gtk_tool_item_new ();
    self->priv->search_item = GTK_WIDGET (tool_box);
    self->priv->search_entry = gtk_search_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (self->priv->search_entry), _("Search"));
    gtk_widget_set_size_request (self->priv->search_entry, FINDER_SEARCH_WIDTH, -1);
    gtk_widget_set_valign (self->priv->search_entry, GTK_ALIGN_CENTER);
    g_signal_connect (self->priv->search_entry, "search-changed",
                      G_CALLBACK (search_entry_changed_cb), self);
    g_signal_connect (self->priv->search_entry, "stop-search",
                      G_CALLBACK (search_entry_stop_cb), self);
    g_signal_connect_after (self->priv->search_entry, "grab-focus",
                            G_CALLBACK (search_entry_grab_focus_cb), self);
    gtk_container_add (GTK_CONTAINER (tool_box), self->priv->search_entry);
    gtk_container_add (GTK_CONTAINER (self->priv->toolbar), GTK_WIDGET (tool_box));
    gtk_widget_show_all (GTK_WIDGET (tool_box));
    gtk_widget_set_margin_left (GTK_WIDGET (tool_box), 6);

    g_signal_connect_swapped (nemo_preferences,
                  "changed",
                  G_CALLBACK (toolbar_update_appearance), self);

	toolbar_update_appearance (self);
}

static void
nemo_toolbar_init (NemoToolbar *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, NEMO_TYPE_TOOLBAR,
						  NemoToolbarPriv);
	self->priv->show_main_bar = TRUE;	
}

static void
nemo_toolbar_get_property (GObject *object,
			       guint property_id,
			       GValue *value,
			       GParamSpec *pspec)
{
	NemoToolbar *self = NEMO_TOOLBAR (object);

	switch (property_id) {
	case PROP_SHOW_LOCATION_ENTRY:
		g_value_set_boolean (value, self->priv->show_location_entry);
		break;
	case PROP_SHOW_MAIN_BAR:
		g_value_set_boolean (value, self->priv->show_main_bar);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
nemo_toolbar_set_property (GObject *object,
			       guint property_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	NemoToolbar *self = NEMO_TOOLBAR (object);

	switch (property_id) {
	case PROP_ACTION_GROUP:
		self->priv->action_group = g_value_dup_object (value);
		break;
	case PROP_SHOW_LOCATION_ENTRY:
		nemo_toolbar_set_show_location_entry (self, g_value_get_boolean (value));
		break;
	case PROP_SHOW_MAIN_BAR:
		nemo_toolbar_set_show_main_bar (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
nemo_toolbar_dispose (GObject *obj)
{
	NemoToolbar *self = NEMO_TOOLBAR (obj);

	g_clear_object (&self->priv->action_group);

    if (self->priv->action_menu != NULL) {
        gtk_widget_destroy (self->priv->action_menu);
        g_clear_object (&self->priv->action_menu);
    }

    if (self->priv->window_ui_manager != NULL) {
        g_signal_handlers_disconnect_by_data (self->priv->window_ui_manager, self);
        g_clear_object (&self->priv->window_ui_manager);
    }

	g_signal_handlers_disconnect_by_func (nemo_preferences,
					      toolbar_update_appearance, self);

	G_OBJECT_CLASS (nemo_toolbar_parent_class)->dispose (obj);
}

static void
nemo_toolbar_class_init (NemoToolbarClass *klass)
{
	GObjectClass *oclass;

	oclass = G_OBJECT_CLASS (klass);
	oclass->get_property = nemo_toolbar_get_property;
	oclass->set_property = nemo_toolbar_set_property;
	oclass->constructed = nemo_toolbar_constructed;
	oclass->dispose = nemo_toolbar_dispose;

	properties[PROP_ACTION_GROUP] =
		g_param_spec_object ("action-group",
				     "The action group",
				     "The action group to get actions from",
				     GTK_TYPE_ACTION_GROUP,
				     G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS);
	properties[PROP_SHOW_LOCATION_ENTRY] =
		g_param_spec_boolean ("show-location-entry",
				      "Whether to show the location entry",
				      "Whether to show the location entry instead of the pathbar",
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	properties[PROP_SHOW_MAIN_BAR] =
		g_param_spec_boolean ("show-main-bar",
				      "Whether to show the main bar",
				      "Whether to show the main toolbar",
				      TRUE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	
    signals[CHECK_ADMIN_LOCATION] =
        g_signal_new ("check-admin-location",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_BOOLEAN, 0);

    signals[SEARCH_ESCAPED] =
        g_signal_new ("search-escaped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SEARCH_TEXT_CHANGED] =
        g_signal_new ("search-text-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (NemoToolbarClass));
	g_object_class_install_properties (oclass, NUM_PROPERTIES, properties);
}

GtkWidget *
nemo_toolbar_new (GtkActionGroup *action_group)
{
	return g_object_new (NEMO_TYPE_TOOLBAR,
			     "action-group", action_group,
			     "orientation", GTK_ORIENTATION_VERTICAL,
			     NULL);
}

void
nemo_toolbar_set_ui_manager (NemoToolbar *self,
                             GtkUIManager *ui_manager)
{
    g_return_if_fail (NEMO_IS_TOOLBAR (self));

    if (self->priv->window_ui_manager != NULL) {
        g_signal_handlers_disconnect_by_data (self->priv->window_ui_manager, self);
        g_clear_object (&self->priv->window_ui_manager);
    }

    if (ui_manager != NULL) {
        self->priv->window_ui_manager = g_object_ref (ui_manager);
        /* las vistas registran sus acciones al montarse */
        g_signal_connect_swapped (ui_manager, "actions-changed",
                                  G_CALLBACK (toolbar_update_arrange_button), self);
        g_signal_connect_swapped (ui_manager, "actions-changed",
                                  G_CALLBACK (toolbar_update_finder_buttons), self);
    }

    toolbar_update_arrange_button (self);
    toolbar_update_finder_buttons (self);
}

void
nemo_toolbar_clear_search (NemoToolbar *self)
{
    g_return_if_fail (NEMO_IS_TOOLBAR (self));

    if (self->priv->search_entry == NULL ||
        gtk_entry_get_text_length (GTK_ENTRY (self->priv->search_entry)) == 0)
        return;

    self->priv->updating_search = TRUE;
    gtk_entry_set_text (GTK_ENTRY (self->priv->search_entry), "");
    self->priv->updating_search = FALSE;
}

gboolean
nemo_toolbar_search_has_focus (NemoToolbar *self)
{
    g_return_val_if_fail (NEMO_IS_TOOLBAR (self), FALSE);

    return self->priv->search_entry != NULL &&
           gtk_widget_has_focus (self->priv->search_entry);
}

/* Cmd+F: enfoca el campo de búsqueda y selecciona su texto. FALSE si no está
 * visible (layout Finder apagado). */
gboolean
nemo_toolbar_activate_search (NemoToolbar *self)
{
    g_return_val_if_fail (NEMO_IS_TOOLBAR (self), FALSE);

    if (self->priv->search_entry == NULL ||
        !gtk_widget_get_visible (self->priv->search_item))
        return FALSE;

    self->priv->select_on_focus = TRUE;
    gtk_widget_grab_focus (self->priv->search_entry);
    self->priv->select_on_focus = FALSE;
    return TRUE;
}

void
nemo_toolbar_focus_search (NemoToolbar *self)
{
    g_return_if_fail (NEMO_IS_TOOLBAR (self));

    gtk_entry_grab_focus_without_selecting (GTK_ENTRY (self->priv->search_entry));
}

GtkWidget *
nemo_toolbar_get_path_bar (NemoToolbar *self)
{
	return self->priv->path_bar;
}

GtkWidget *
nemo_toolbar_get_location_bar (NemoToolbar *self)
{
	return self->priv->location_bar;
}

gboolean
nemo_toolbar_get_show_location_entry (NemoToolbar *self)
{
	return self->priv->show_location_entry;
}

void
nemo_toolbar_set_show_main_bar (NemoToolbar *self,
				    gboolean show_main_bar)
{
	if (show_main_bar != self->priv->show_main_bar) {
		self->priv->show_main_bar = show_main_bar;
		toolbar_update_appearance (self);

		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_MAIN_BAR]);
	}
}

void
nemo_toolbar_set_show_location_entry (NemoToolbar *self,
					  gboolean show_location_entry)
{
	if (show_location_entry != self->priv->show_location_entry) {
		self->priv->show_location_entry = show_location_entry;
		toolbar_update_appearance (self);

		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_LOCATION_ENTRY]);
	}
}

void
nemo_toolbar_update_for_location (NemoToolbar *self)
{
    toolbar_update_appearance (self);
}
