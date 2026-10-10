/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Ventana de Preferences de Fynder (General, Sidebar, Advanced).
 */

#include <config.h>

#include "nemo-finder-preferences.h"

#include "nemo-file-management-properties.h"

#include <glib/gi18n.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>

static GtkWidget *preferences_window = NULL;

static const struct {
	const char *id;
	const char *label;
} new_window_targets[] = {
	{ "home",      N_("Home") },
	{ "documents", N_("Documents") },
	{ "desktop",   N_("Desktop") },
	{ "downloads", N_("Downloads") },
	{ "recents",   N_("Recents") },
	{ "computer",  N_("Computer") },
};

static const struct {
	const char *token;
	const char *label;
} favorites_items[] = {
	{ "recent",    N_("Recents") },
	{ "desktop",   N_("Desktop") },
	{ "documents", N_("Documents") },
	{ "downloads", N_("Downloads") },
	{ "movies",    N_("Movies") },
	{ "music",     N_("Music") },
	{ "pictures",  N_("Pictures") },
	{ "home",      N_("Home") },
};

static const struct {
	const char *token;
	const char *label; /* NULL: nombre de la raíz según el ajuste */
} locations_items[] = {
	{ "computer", NULL },
	{ "disks",    N_("External disks") },
	{ "network",  N_("Network") },
	{ "trash",    N_("Trash") },
};

static GtkWidget *
section_label (const char *text)
{
	GtkWidget *label = gtk_label_new (NULL);
	char *markup = g_markup_printf_escaped ("<b>%s</b>", text);

	gtk_label_set_markup (GTK_LABEL (label), markup);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	g_free (markup);
	return label;
}

static GtkWidget *
page_box (void)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);

	gtk_container_set_border_width (GTK_CONTAINER (box), 18);
	return box;
}

static GtkWidget *
bound_check (const char *label,
             const char *key)
{
	GtkWidget *check = gtk_check_button_new_with_label (label);

	g_settings_bind (nemo_preferences, key, check, "active", G_SETTINGS_BIND_DEFAULT);
	return check;
}

static GtkWidget *
bound_desktop_check (const char *label,
                     const char *key)
{
	GtkWidget *check = gtk_check_button_new_with_label (label);

	g_settings_bind (nemo_desktop_preferences, key, check, "active", G_SETTINGS_BIND_DEFAULT);
	return check;
}

/* ---------- General ---------- */

static GtkWidget *
build_general_page (void)
{
	GtkWidget *box = page_box ();
	GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *combo = gtk_combo_box_text_new ();
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (new_window_targets); i++)
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), new_window_targets[i].id,
		                           _(new_window_targets[i].label));
	g_settings_bind (nemo_preferences, NEMO_PREFERENCES_FINDER_NEW_WINDOW_LOCATION,
	                 combo, "active-id", G_SETTINGS_BIND_DEFAULT);

	gtk_box_pack_start (GTK_BOX (row), gtk_label_new (_("New Fynder windows show:")), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), combo, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);

	gtk_box_pack_start (GTK_BOX (box), section_label (_("Show these items on the desktop:")), FALSE, FALSE, 4);
	gtk_box_pack_start (GTK_BOX (box), bound_desktop_check (_("Hard disks"), "finder-desktop-hard-disks"), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), bound_desktop_check (_("External disks"), "finder-desktop-external-disks"), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), bound_desktop_check (_("CDs and DVDs"), "finder-desktop-optical-disks"), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), bound_desktop_check (_("Connected servers"), "finder-desktop-servers"), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 6);

	gtk_box_pack_start (GTK_BOX (box), bound_check (_("Open folders in tabs instead of new windows"),
	                                                NEMO_PREFERENCES_FINDER_OPEN_IN_TABS),
	                    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), bound_check (_("Restore the last windows and tabs on startup"),
	                                                NEMO_PREFERENCES_RESTORE_TABS_ON_STARTUP),
	                    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), bound_check (_("Show the full path in the title bar"),
	                                                NEMO_PREFERENCES_SHOW_FULL_PATH_TITLES),
	                    FALSE, FALSE, 0);
	return box;
}

/* ---------- Sidebar ---------- */

static void
sidebar_item_toggled_cb (GtkToggleButton *button,
                         gpointer user_data)
{
	const char *token = user_data;
	gchar **hidden = g_settings_get_strv (nemo_preferences,
	                                      NEMO_PREFERENCES_SIDEBAR_FINDER_HIDDEN_ITEMS);
	GPtrArray *list = g_ptr_array_new ();
	gboolean visible = gtk_toggle_button_get_active (button);
	gboolean found = FALSE;
	gint i;

	for (i = 0; hidden[i] != NULL; i++) {
		if (g_strcmp0 (hidden[i], token) == 0) {
			found = TRUE;
			if (visible)
				continue; /* se vuelve a mostrar: sale de la lista */
		}
		g_ptr_array_add (list, hidden[i]);
	}
	if (!visible && !found)
		g_ptr_array_add (list, (gpointer) token);
	g_ptr_array_add (list, NULL);

	g_settings_set_strv (nemo_preferences, NEMO_PREFERENCES_SIDEBAR_FINDER_HIDDEN_ITEMS,
	                     (const gchar * const *) list->pdata);

	g_ptr_array_free (list, TRUE);
	g_strfreev (hidden);
}

static GtkWidget *
sidebar_check (const char *token,
               const char *label)
{
	GtkWidget *check = gtk_check_button_new_with_label (label);
	gchar **hidden = g_settings_get_strv (nemo_preferences,
	                                      NEMO_PREFERENCES_SIDEBAR_FINDER_HIDDEN_ITEMS);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
	                              !g_strv_contains ((const gchar * const *) hidden, token));
	g_strfreev (hidden);

	/* los tokens son literales estáticos: valen como user_data */
	g_signal_connect (check, "toggled", G_CALLBACK (sidebar_item_toggled_cb), (gpointer) token);
	return check;
}

static GtkWidget *
build_sidebar_page (void)
{
	GtkWidget *box = page_box ();
	gsize i;

	gtk_box_pack_start (GTK_BOX (box), section_label (_("Show these items in the sidebar:")), FALSE, FALSE, 0);

	gtk_box_pack_start (GTK_BOX (box), section_label (_("Favorites")), FALSE, FALSE, 4);
	for (i = 0; i < G_N_ELEMENTS (favorites_items); i++)
		gtk_box_pack_start (GTK_BOX (box),
		                    sidebar_check (favorites_items[i].token, _(favorites_items[i].label)),
		                    FALSE, FALSE, 0);

	gtk_box_pack_start (GTK_BOX (box), section_label (_("Locations")), FALSE, FALSE, 4);
	for (i = 0; i < G_N_ELEMENTS (locations_items); i++) {
		char *root_name = NULL;
		const char *label = locations_items[i].label != NULL ? _(locations_items[i].label)
		                                                     : (root_name = nemo_get_root_display_name ());

		gtk_box_pack_start (GTK_BOX (box), sidebar_check (locations_items[i].token, label),
		                    FALSE, FALSE, 0);
		g_free (root_name);
	}

	return box;
}

/* ---------- Advanced ---------- */

static void
more_options_clicked_cb (GtkButton *button,
                         gpointer user_data)
{
	nemo_file_management_properties_dialog_show (GTK_WINDOW (preferences_window), NULL);
}

static GtkWidget *
build_advanced_page (void)
{
	GtkWidget *box = page_box ();
	GtkWidget *button = gtk_button_new_with_label (_("More Options…"));

	gtk_box_pack_start (GTK_BOX (box), bound_check (_("Show all filename extensions"),
	                                                NEMO_PREFERENCES_FINDER_SHOW_ALL_EXTENSIONS),
	                    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), bound_check (_("Show warning before emptying the Trash"),
	                                                NEMO_PREFERENCES_CONFIRM_TRASH),
	                    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), bound_check (_("Keep folders on top when sorting by name"),
	                                                NEMO_PREFERENCES_SORT_DIRECTORIES_FIRST),
	                    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), bound_check (_("Show hidden files"),
	                                                NEMO_PREFERENCES_SHOW_HIDDEN_FILES),
	                    FALSE, FALSE, 0);

	gtk_widget_set_halign (button, GTK_ALIGN_START);
	gtk_widget_set_margin_top (button, 12);
	g_signal_connect (button, "clicked", G_CALLBACK (more_options_clicked_cb), NULL);
	gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
	return box;
}

/* ---------- ventana ---------- */

static void
visible_child_changed_cb (GtkStack *stack,
                          GParamSpec *pspec,
                          GtkWindow *window)
{
	const char *name = gtk_stack_get_visible_child_name (stack);

	/* como el Finder, el título es el de la pestaña */
	if (name != NULL)
		gtk_window_set_title (window, name);
}

void
nemo_finder_preferences_show (GtkWindow *parent)
{
	GtkWidget *vbox, *stack, *switcher;

	if (preferences_window != NULL) {
		gtk_window_present (GTK_WINDOW (preferences_window));
		return;
	}

	preferences_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (preferences_window), 420, -1);
	gtk_window_set_resizable (GTK_WINDOW (preferences_window), FALSE);
	gtk_window_set_type_hint (GTK_WINDOW (preferences_window), GDK_WINDOW_TYPE_HINT_DIALOG);
	if (parent != NULL)
		gtk_window_set_transient_for (GTK_WINDOW (preferences_window), parent);
	g_signal_connect (preferences_window, "destroy",
	                  G_CALLBACK (gtk_widget_destroyed), &preferences_window);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add (GTK_CONTAINER (preferences_window), vbox);

	stack = gtk_stack_new ();
	gtk_stack_set_vhomogeneous (GTK_STACK (stack), FALSE);
	switcher = gtk_stack_switcher_new ();
	gtk_stack_switcher_set_stack (GTK_STACK_SWITCHER (switcher), GTK_STACK (stack));
	gtk_widget_set_halign (switcher, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top (switcher, 12);

	gtk_stack_add_titled (GTK_STACK (stack), build_general_page (), "General", _("General"));
	gtk_stack_add_titled (GTK_STACK (stack), build_sidebar_page (), "Sidebar", _("Sidebar"));
	gtk_stack_add_titled (GTK_STACK (stack), build_advanced_page (), "Advanced", _("Advanced"));
	g_signal_connect (stack, "notify::visible-child", G_CALLBACK (visible_child_changed_cb),
	                  preferences_window);

	gtk_box_pack_start (GTK_BOX (vbox), switcher, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), stack, TRUE, TRUE, 0);

	gtk_window_set_title (GTK_WINDOW (preferences_window), _("General"));
	gtk_widget_show_all (preferences_window);
}
