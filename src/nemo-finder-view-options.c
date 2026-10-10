/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * «Show View Options» de Fynder.
 */

#include <config.h>

#include "nemo-finder-view-options.h"

#include "nemo-list-view.h"
#include "nemo-view.h"
#include "nemo-window-private.h"
#include "nemo-window-slot.h"

#include <glib/gi18n.h>
#include <libnemo-private/nemo-global-preferences.h>

static GtkWidget *options_window = NULL;

static const struct {
	const char *attribute;
	const char *label;
} sort_options[] = {
	{ "name",          N_("Name") },
	{ "type",          N_("Kind") },
	{ "date_modified", N_("Date Modified") },
	{ "size",          N_("Size") },
};

static void
zoom_value_changed_cb (GtkRange *range,
                       NemoView *view)
{
	nemo_view_zoom_to_level (view, (NemoZoomLevel) gtk_range_get_value (range));
}

static void
sort_combo_changed_cb (GtkComboBox *combo,
                       NemoListView *view)
{
	const char *attribute = gtk_combo_box_get_active_id (combo);
	const char *current;
	gboolean reversed = FALSE;

	if (attribute == NULL)
		return;

	if (nemo_list_view_get_sort (view, &current, &reversed) && g_strcmp0 (current, attribute) == 0)
		return;

	nemo_list_view_set_sort (view, attribute,
	                         nemo_file_is_date_sort_attribute_q (g_quark_from_string (attribute)));
}

static void
reverse_toggled_cb (GtkToggleButton *button,
                    NemoListView *view)
{
	const char *current;
	gboolean reversed;

	if (nemo_list_view_get_sort (view, &current, &reversed) &&
	    reversed != gtk_toggle_button_get_active (button))
		nemo_list_view_set_sort (view, current, gtk_toggle_button_get_active (button));
}

static GtkWidget *
labeled_row (const char *text,
             GtkWidget *widget)
{
	GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	GtkWidget *label = gtk_label_new (text);

	gtk_widget_set_size_request (label, 96, -1);
	gtk_widget_set_halign (label, GTK_ALIGN_END);
	gtk_box_pack_start (GTK_BOX (row), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), widget, TRUE, TRUE, 0);
	return row;
}

void
nemo_finder_view_options_show (NemoWindow *window)
{
	NemoWindowSlot *slot = nemo_window_get_active_slot (window);
	NemoView *view = (slot != NULL) ? slot->content_view : NULL;
	GtkWidget *box, *check;

	if (view == NULL)
		return;

	if (options_window != NULL)
		gtk_widget_destroy (options_window);

	options_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (options_window), _("View Options"));
	gtk_window_set_resizable (GTK_WINDOW (options_window), FALSE);
	gtk_window_set_type_hint (GTK_WINDOW (options_window), GDK_WINDOW_TYPE_HINT_UTILITY);
	gtk_window_set_transient_for (GTK_WINDOW (options_window), GTK_WINDOW (window));
	/* la ventana se refiere a la vista de ahora: si ésta se va, se cierra */
	g_signal_connect (options_window, "destroy", G_CALLBACK (gtk_widget_destroyed), &options_window);
	g_signal_connect_object (view, "destroy", G_CALLBACK (gtk_widget_destroy), options_window,
	                         G_CONNECT_SWAPPED);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_set_border_width (GTK_CONTAINER (box), 18);
	gtk_container_add (GTK_CONTAINER (options_window), box);

	if (nemo_view_supports_zooming (view)) {
		GtkWidget *scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
		                                              NEMO_ZOOM_LEVEL_SMALLEST,
		                                              NEMO_ZOOM_LEVEL_LARGEST, 1);

		gtk_scale_set_draw_value (GTK_SCALE (scale), FALSE);
		gtk_scale_set_digits (GTK_SCALE (scale), 0);
		gtk_range_set_value (GTK_RANGE (scale), nemo_view_get_zoom_level (view));
		g_signal_connect (scale, "value-changed", G_CALLBACK (zoom_value_changed_cb), view);
		gtk_widget_set_size_request (scale, 200, -1);
		gtk_box_pack_start (GTK_BOX (box), labeled_row (_("Icon size:"), scale), FALSE, FALSE, 0);
	}

	if (NEMO_IS_LIST_VIEW (view)) {
		GtkWidget *combo = gtk_combo_box_text_new ();
		const char *current = NULL;
		gboolean reversed = FALSE;
		gsize i;

		nemo_list_view_get_sort (NEMO_LIST_VIEW (view), &current, &reversed);
		for (i = 0; i < G_N_ELEMENTS (sort_options); i++)
			gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), sort_options[i].attribute,
			                           _(sort_options[i].label));
		if (current != NULL)
			gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), current);
		g_signal_connect (combo, "changed", G_CALLBACK (sort_combo_changed_cb), view);
		gtk_box_pack_start (GTK_BOX (box), labeled_row (_("Sort By:"), combo), FALSE, FALSE, 0);

		check = gtk_check_button_new_with_label (_("Reverse order"));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), reversed);
		g_signal_connect (check, "toggled", G_CALLBACK (reverse_toggled_cb), view);
		gtk_box_pack_start (GTK_BOX (box), labeled_row ("", check), FALSE, FALSE, 0);
	}

	check = gtk_check_button_new_with_label (_("Keep folders on top"));
	g_settings_bind (nemo_preferences, NEMO_PREFERENCES_SORT_DIRECTORIES_FIRST,
	                 check, "active", G_SETTINGS_BIND_DEFAULT);
	gtk_box_pack_start (GTK_BOX (box), labeled_row ("", check), FALSE, FALSE, 0);

	check = gtk_check_button_new_with_label (_("Dim hidden files"));
	g_settings_bind (nemo_preferences, "finder-dim-hidden-files",
	                 check, "active", G_SETTINGS_BIND_DEFAULT);
	gtk_box_pack_start (GTK_BOX (box), labeled_row ("", check), FALSE, FALSE, 0);

	gtk_widget_show_all (options_window);
}
