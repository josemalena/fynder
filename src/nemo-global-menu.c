/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Menú global. Ver nemo-global-menu.h.
 *
 * Diseño:
 * - Hay UN GMenu compartido por toda la aplicación, exportado una sola vez
 *   con gtk_application_set_menubar(). Se rellena a partir de la barra de
 *   menú real (GtkUIManager) de la ventana activa; al cambiar de ventana o
 *   de contenido se vacía y se vuelve a llenar sobre el mismo objeto, así
 *   que quien lo escucha recibe items-changed y no una barra nueva.
 * - Cada ventana registra sus propias acciones «win.*». Los nombres salen de
 *   la posición del ítem en la barra, no del puntero, para que todas las
 *   ventanas respondan a los mismos nombres y el modelo compartido sirva
 *   para cualquiera.
 * - Cada ítem de menú hoja se convierte en una acción que activa su
 *   GtkMenuItem; los de tipo check llevan estado booleano; la sensibilidad
 *   del ítem es el «enabled» de la acción.
 */

#include <config.h>

#include "nemo-global-menu.h"

#include "nemo-desktop-window.h"
#include "nemo-window-private.h"

#include <gdk/gdkx.h>
#include <glib/gi18n.h>
#include <libnemo-private/nemo-global-preferences.h>

#define DATA_KEY "nemo-global-menu"
#define APP_ITEM_KEY "nemo-global-menu-app-item"

typedef struct {
	NemoWindow *window;          /* débil: el objeto vive en la ventana */
	GtkUIManager *ui_manager;
	GHashTable *actions;         /* nombre (char *) -> ItemData * */
	guint rebuild_id;
} GlobalMenu;

typedef struct {
	GlobalMenu *gm;
	GtkWidget *widget;
	GSimpleAction *action;       /* referencia propia */
	char *name;
} ItemData;

/* Modelo compartido por toda la aplicación */
static GMenu *shared_menu = NULL;
static GtkApplication *shared_app = NULL;
static char *shared_signature = NULL;

static void schedule_rebuild (GlobalMenu *gm);

gboolean
nemo_global_menu_hides_menubar (void)
{
	return g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_GLOBAL_MENU);
}

/* ---------- ítems y acciones ---------- */

static void widget_destroyed_cb (gpointer data, GObject *where_the_object_was);

static void
item_data_free (gpointer data)
{
	ItemData *item = data;

	if (item->widget != NULL) {
		g_signal_handlers_disconnect_by_data (item->widget, item);
		g_object_weak_unref (G_OBJECT (item->widget), widget_destroyed_cb, item);
	}
	g_object_unref (item->action);
	g_free (item->name);
	g_free (item);
}

static void
widget_destroyed_cb (gpointer data,
                     GObject *where_the_object_was)
{
	ItemData *item = data;
	GlobalMenu *gm = item->gm;

	/* el widget ya no existe: se retira la acción y se rehace el modelo */
	item->widget = NULL;
	if (gm->window != NULL)
		g_action_map_remove_action (G_ACTION_MAP (gm->window), item->name);
	g_hash_table_remove (gm->actions, item->name);
	schedule_rebuild (gm);
}

static void
item_activate_cb (GSimpleAction *action,
                  GVariant *parameter,
                  gpointer user_data)
{
	ItemData *item = user_data;

	if (item->widget != NULL)
		gtk_menu_item_activate (GTK_MENU_ITEM (item->widget));
}

static void
item_change_state_cb (GSimpleAction *action,
                      GVariant *value,
                      gpointer user_data)
{
	ItemData *item = user_data;

	/* El estado lo fija notify::active cuando el GtkCheckMenuItem cambia. */
	if (item->widget != NULL)
		gtk_menu_item_activate (GTK_MENU_ITEM (item->widget));
}

static void
item_sensitive_cb (GObject *object,
                   GParamSpec *pspec,
                   gpointer user_data)
{
	ItemData *item = user_data;

	g_simple_action_set_enabled (item->action, gtk_widget_get_sensitive (GTK_WIDGET (object)));
}

static void
item_active_cb (GObject *object,
                GParamSpec *pspec,
                gpointer user_data)
{
	ItemData *item = user_data;

	g_simple_action_set_state (item->action,
	                           g_variant_new_boolean (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (object))));
}

static void
item_structure_cb (GObject *object,
                   GParamSpec *pspec,
                   gpointer user_data)
{
	ItemData *item = user_data;

	schedule_rebuild (item->gm);
}

static ItemData *
ensure_item (GlobalMenu *gm,
             GtkWidget *widget,
             const char *name,
             GHashTable *used)
{
	ItemData *item = g_hash_table_lookup (gm->actions, name);

	if (item != NULL && item->widget != widget) {
		/* mismo lugar, otro widget (el menú se rehízo): se reemplaza */
		g_action_map_remove_action (G_ACTION_MAP (gm->window), name);
		g_hash_table_remove (gm->actions, name);
		item = NULL;
	}

	if (item == NULL) {
		gboolean is_check = GTK_IS_CHECK_MENU_ITEM (widget);

		item = g_new0 (ItemData, 1);
		item->gm = gm;
		item->widget = widget;
		item->name = g_strdup (name);

		if (is_check) {
			item->action = g_simple_action_new_stateful (name, NULL,
				g_variant_new_boolean (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget))));
			g_signal_connect (item->action, "change-state",
			                  G_CALLBACK (item_change_state_cb), item);
			g_signal_connect (widget, "notify::active",
			                  G_CALLBACK (item_active_cb), item);
		} else {
			item->action = g_simple_action_new (name, NULL);
			g_signal_connect (item->action, "activate",
			                  G_CALLBACK (item_activate_cb), item);
		}

		g_signal_connect (widget, "notify::sensitive", G_CALLBACK (item_sensitive_cb), item);
		g_signal_connect (widget, "notify::visible", G_CALLBACK (item_structure_cb), item);
		g_signal_connect (widget, "notify::label", G_CALLBACK (item_structure_cb), item);
		g_object_weak_ref (G_OBJECT (widget), widget_destroyed_cb, item);

		g_action_map_add_action (G_ACTION_MAP (gm->window), G_ACTION (item->action));
		g_hash_table_insert (gm->actions, item->name, item);
	}

	g_simple_action_set_enabled (item->action, gtk_widget_get_sensitive (widget));
	g_hash_table_add (used, item->name);

	return item;
}

/* ---------- construcción del modelo ---------- */

static char *
menu_label (GtkMenuItem *item)
{
	const char *label = gtk_menu_item_get_label (item);

	if (label == NULL || label[0] == '\0')
		return NULL;

	/* en GMenu el guion bajo marca el mnemónico: se escapa si no lo era */
	if (!gtk_menu_item_get_use_underline (item)) {
		GString *escaped = g_string_new (NULL);
		const char *p;

		for (p = label; *p != '\0'; p++) {
			if (*p == '_')
				g_string_append_c (escaped, '_');
			g_string_append_c (escaped, *p);
		}
		return g_string_free (escaped, FALSE);
	}

	return g_strdup (label);
}

static void
set_accel (GMenuItem *menu_item,
           GtkMenuItem *widget)
{
	const char *path = gtk_menu_item_get_accel_path (widget);
	GtkAccelKey key;

	if (path != NULL && gtk_accel_map_lookup_entry (path, &key) && key.accel_key != 0) {
		char *accel = gtk_accelerator_name (key.accel_key, key.accel_mods);

		g_menu_item_set_attribute (menu_item, "accel", "s", accel);
		g_free (accel);
	}
}

static void
flush_section (GMenu *target,
               GMenu **section)
{
	if (g_menu_model_get_n_items (G_MENU_MODEL (*section)) > 0) {
		g_menu_append_section (target, NULL, G_MENU_MODEL (*section));
		g_object_unref (*section);
		*section = g_menu_new ();
	}
}

static void
fill_from_shell (GlobalMenu *gm,
                 GtkMenuShell *shell,
                 GMenu *target,
                 GHashTable *used,
                 const char *path,
                 gboolean top_level)
{
	GList *children = gtk_container_get_children (GTK_CONTAINER (shell));
	GMenu *section = top_level ? NULL : g_menu_new ();
	GList *l;
	guint index = 0;

	for (l = children; l != NULL; l = l->next, index++) {
		GtkWidget *child = l->data;
		GMenu *dest = top_level ? target : section;
		GtkWidget *submenu;
		char *label, *child_path;

		/* los que oculta GtkUIManager (acciones invisibles) no se publican */
		if (!gtk_widget_get_visible (child))
			continue;

		if (GTK_IS_SEPARATOR_MENU_ITEM (child)) {
			if (!top_level)
				flush_section (target, &section);
			continue;
		}

		if (!GTK_IS_MENU_ITEM (child))
			continue;

		/* ya van en el menú de la aplicación */
		if (g_object_get_data (G_OBJECT (child), APP_ITEM_KEY) != NULL)
			continue;

		label = menu_label (GTK_MENU_ITEM (child));
		if (label == NULL)
			continue;

		child_path = g_strdup_printf ("%s_%u", path, index);
		submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (child));

		if (submenu != NULL) {
			GMenu *sub = g_menu_new ();

			fill_from_shell (gm, GTK_MENU_SHELL (submenu), sub, used, child_path, FALSE);
			if (g_menu_model_get_n_items (G_MENU_MODEL (sub)) > 0)
				g_menu_append_submenu (dest, label, G_MENU_MODEL (sub));
			g_object_unref (sub);
		} else {
			ItemData *item = ensure_item (gm, child, child_path, used);
			char *detailed = g_strdup_printf ("win.%s", item->name);
			GMenuItem *menu_item = g_menu_item_new (label, detailed);

			set_accel (menu_item, GTK_MENU_ITEM (child));
			g_menu_append_item (dest, menu_item);
			g_object_unref (menu_item);
			g_free (detailed);
		}

		g_free (label);
		g_free (child_path);
	}

	if (!top_level) {
		if (g_menu_model_get_n_items (G_MENU_MODEL (section)) > 0)
			g_menu_append_section (target, NULL, G_MENU_MODEL (section));
		g_object_unref (section);
	}

	g_list_free (children);
}

/* Firma textual de un modelo (etiquetas, acciones, atajos y estructura). Sirve
 * para no tocar el modelo compartido si su contenido no cambió: cada cambio
 * obliga a quien lo escucha a volver a suscribirse a los grupos de submenús. */
static void
append_signature (GMenuModel *model,
                  GString *out)
{
	gint i, n = g_menu_model_get_n_items (model);

	g_string_append_c (out, '(');
	for (i = 0; i < n; i++) {
		GMenuModel *link;
		GVariant *value;
		const gchar *attrs[] = { G_MENU_ATTRIBUTE_LABEL, G_MENU_ATTRIBUTE_ACTION, "accel", NULL };
		gint a;

		for (a = 0; attrs[a] != NULL; a++) {
			value = g_menu_model_get_item_attribute_value (model, i, attrs[a], G_VARIANT_TYPE_STRING);
			if (value != NULL) {
				g_string_append (out, g_variant_get_string (value, NULL));
				g_variant_unref (value);
			}
			g_string_append_c (out, '\x1f');
		}

		link = g_menu_model_get_item_link (model, i, G_MENU_LINK_SUBMENU);
		if (link != NULL) {
			g_string_append_c (out, 'S');
			append_signature (link, out);
			g_object_unref (link);
		}
		link = g_menu_model_get_item_link (model, i, G_MENU_LINK_SECTION);
		if (link != NULL) {
			g_string_append_c (out, 'N');
			append_signature (link, out);
			g_object_unref (link);
		}
		g_string_append_c (out, '\x1e');
	}
	g_string_append_c (out, ')');
}

static gboolean
window_owns_model (GlobalMenu *gm)
{
	return gm->window != NULL && gtk_window_is_active (GTK_WINDOW (gm->window));
}

/* Menú de la aplicación («Fynder»), como el del Finder: Acerca de, Ajustes y
 * Vaciar papelera. Sus ítems salen de los mismos widgets de GtkUIManager, que
 * se quitan de su lugar habitual (Edit, Help, File). El atributo
 * x-constanza-app-menu avisa al applet de que es el menú de la aplicación. */
static const struct {
	const char *widget_path;
	const char *label;
	const char *name;
	gboolean separator_after;
} app_menu_items[] = {
	{ "/MenuBar/Help/About Nemo",                              N_("About Fynder"),  "app_about", TRUE  },
	{ "/MenuBar/Edit/Preferences",                             N_("Preferences…"),  "app_prefs", TRUE  },
	{ "/MenuBar/File/Global File Items Placeholder/Empty Trash", N_("Empty Trash…"), "app_trash", FALSE },
};

static void
mark_app_items (GlobalMenu *gm)
{
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (app_menu_items); i++) {
		GtkWidget *widget = gtk_ui_manager_get_widget (gm->ui_manager, app_menu_items[i].widget_path);

		if (widget != NULL)
			g_object_set_data (G_OBJECT (widget), APP_ITEM_KEY, GINT_TO_POINTER (1));
	}
}

static GMenu *
build_app_menu (GlobalMenu *gm,
                GHashTable *used)
{
	GMenu *app = g_menu_new ();
	GMenu *section = g_menu_new ();
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (app_menu_items); i++) {
		GtkWidget *widget = gtk_ui_manager_get_widget (gm->ui_manager, app_menu_items[i].widget_path);
		ItemData *item;
		char *detailed;
		GMenuItem *menu_item;

		if (widget == NULL || !GTK_IS_MENU_ITEM (widget))
			continue;

		item = ensure_item (gm, widget, app_menu_items[i].name, used);
		detailed = g_strdup_printf ("win.%s", item->name);
		menu_item = g_menu_item_new (_(app_menu_items[i].label), detailed);
		/* como en el Finder, los ítems fuera de contexto (Empty Trash… con la
		 * papelera vacía) se publican igual y salen en gris: ensure_item copia
		 * la sensibilidad del widget aunque esté oculto. */
		set_accel (menu_item, GTK_MENU_ITEM (widget));
		g_menu_append_item (section, menu_item);
		g_object_unref (menu_item);
		g_free (detailed);

		if (app_menu_items[i].separator_after)
			flush_section (app, &section);
	}

	flush_section (app, &section);
	g_object_unref (section);
	return app;
}

static void
rebuild_now (GlobalMenu *gm)
{
	GHashTable *used;
	GHashTableIter iter;
	gpointer key;
	GList *stale = NULL, *l;
	GMenu *fresh;

	if (gm->window == NULL || gm->ui_manager == NULL)
		return;

	used = g_hash_table_new (g_str_hash, g_str_equal);

	/* Siempre se actualizan las acciones de la ventana; el modelo compartido
	 * solo lo escribe la ventana activa. */
	fresh = g_menu_new ();
	{
		GtkWidget *menubar = gtk_ui_manager_get_widget (gm->ui_manager, "/MenuBar");

		if (GTK_IS_MENU_SHELL (menubar)) {
			GMenu *app = NULL;

			mark_app_items (gm);
			app = build_app_menu (gm, used);
			if (g_menu_model_get_n_items (G_MENU_MODEL (app)) > 0) {
				GMenuItem *app_item = g_menu_item_new_submenu ("Fynder",
				                                               G_MENU_MODEL (app));

				g_menu_item_set_attribute (app_item, "x-constanza-app-menu", "b", TRUE);
				g_menu_append_item (fresh, app_item);
				g_object_unref (app_item);
			}
			g_object_unref (app);

			fill_from_shell (gm, GTK_MENU_SHELL (menubar), fresh, used, "m", TRUE);
		}
	}

	/* acciones que ya no tienen ítem */
	g_hash_table_iter_init (&iter, gm->actions);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		if (!g_hash_table_contains (used, key))
			stale = g_list_prepend (stale, g_strdup (key));
	}
	for (l = stale; l != NULL; l = l->next) {
		g_action_map_remove_action (G_ACTION_MAP (gm->window), l->data);
		g_hash_table_remove (gm->actions, l->data);
	}
	g_list_free_full (stale, g_free);

	if (shared_menu != NULL && window_owns_model (gm)) {
		guint i, n = g_menu_model_get_n_items (G_MENU_MODEL (fresh));
		GString *signature = g_string_new (NULL);

		append_signature (G_MENU_MODEL (fresh), signature);
		if (g_strcmp0 (shared_signature, signature->str) == 0) {
			/* mismo contenido: las acciones ya se actualizaron, el modelo se queda */
			g_string_free (signature, TRUE);
			g_object_unref (fresh);
			g_hash_table_unref (used);
			return;
		}
		g_free (shared_signature);
		shared_signature = g_string_free (signature, FALSE);

		g_menu_remove_all (shared_menu);
		for (i = 0; i < n; i++) {
			GMenuItem *item = g_menu_item_new_from_model (G_MENU_MODEL (fresh), i);

			g_menu_append_item (shared_menu, item);
			g_object_unref (item);
		}
	}

	g_object_unref (fresh);
	g_hash_table_unref (used);
}

static gboolean
rebuild_idle_cb (gpointer data)
{
	GlobalMenu *gm = data;

	gm->rebuild_id = 0;
	rebuild_now (gm);

	return G_SOURCE_REMOVE;
}

static void
schedule_rebuild (GlobalMenu *gm)
{
	if (!nemo_global_menu_hides_menubar () || gm->rebuild_id != 0)
		return;

	gm->rebuild_id = g_idle_add_full (G_PRIORITY_LOW, rebuild_idle_cb, gm, NULL);
}

/* ---------- publicación ---------- */

/* GTK publica las propiedades _GTK_* de la ventana al realizarla si la app
 * ya tiene barra de menú. Si la ventana ya estaba realizada cuando se
 * exportó el modelo, se comprueba y, si faltan, se completan. */
static void
ensure_x_properties (GlobalMenu *gm)
{
	GdkWindow *gdk_window;
	GApplication *app;
	GDBusConnection *connection;
	const char *app_path;
	GdkAtom string_type, atom;
	guchar *data = NULL;

	if (gm->window == NULL || !gtk_widget_get_realized (GTK_WIDGET (gm->window)))
		return;

	gdk_window = gtk_widget_get_window (GTK_WIDGET (gm->window));
	if (!GDK_IS_X11_WINDOW (gdk_window))
		return;

	app = G_APPLICATION (gtk_window_get_application (GTK_WINDOW (gm->window)));
	if (app == NULL)
		return;

	connection = g_application_get_dbus_connection (app);
	app_path = g_application_get_dbus_object_path (app);
	if (connection == NULL || app_path == NULL)
		return;

	string_type = gdk_atom_intern ("UTF8_STRING", FALSE);
	atom = gdk_atom_intern ("_GTK_MENUBAR_OBJECT_PATH", FALSE);

	if (gdk_property_get (gdk_window, atom, string_type, 0, 1024, FALSE,
	                      NULL, NULL, NULL, &data) && data != NULL) {
		g_free (data);
		return;
	}

	{
		char *menubar_path = g_strconcat (app_path, "/menus/menubar", NULL);
		char *window_path = g_strdup_printf ("%s/window/%u", app_path,
		                                     gtk_application_window_get_id (GTK_APPLICATION_WINDOW (gm->window)));

		gdk_x11_window_set_utf8_property (gdk_window, "_GTK_UNIQUE_BUS_NAME",
		                                  g_dbus_connection_get_unique_name (connection));
		gdk_x11_window_set_utf8_property (gdk_window, "_GTK_APPLICATION_OBJECT_PATH", app_path);
		gdk_x11_window_set_utf8_property (gdk_window, "_GTK_MENUBAR_OBJECT_PATH", menubar_path);
		gdk_x11_window_set_utf8_property (gdk_window, "_GTK_WINDOW_OBJECT_PATH", window_path);

		g_free (menubar_path);
		g_free (window_path);
	}
}

static void
sync_publication (GlobalMenu *gm)
{
	GtkApplication *app;

	if (gm->window == NULL)
		return;

	app = gtk_window_get_application (GTK_WINDOW (gm->window));
	if (app == NULL)
		return;

	if (nemo_global_menu_hides_menubar ()) {
		if (shared_menu == NULL) {
			shared_menu = g_menu_new ();
			shared_app = app;
			gtk_application_set_menubar (app, G_MENU_MODEL (shared_menu));
		}

		/* el panel muestra la barra; la ventana no la duplica */
		gtk_application_window_set_show_menubar (GTK_APPLICATION_WINDOW (gm->window), FALSE);
		nemo_window_sync_menu_bar (gm->window);

		rebuild_now (gm);
		ensure_x_properties (gm);
	} else {
		if (shared_menu != NULL && shared_app == app) {
			gtk_application_set_menubar (app, NULL);
			g_clear_object (&shared_menu);
			g_clear_pointer (&shared_signature, g_free);
			shared_app = NULL;
		}
		nemo_window_sync_menu_bar (gm->window);
	}
}

static void
global_menu_pref_changed_cb (GlobalMenu *gm)
{
	sync_publication (gm);
}

static void
window_active_changed_cb (GlobalMenu *gm)
{
	if (nemo_global_menu_hides_menubar () && window_owns_model (gm)) {
		rebuild_now (gm);
		ensure_x_properties (gm);
	}
}

static void
window_realized_cb (GlobalMenu *gm)
{
	ensure_x_properties (gm);
}

static void
ui_changed_cb (GlobalMenu *gm)
{
	schedule_rebuild (gm);
}

static void
global_menu_free (gpointer data)
{
	GlobalMenu *gm = data;

	g_clear_handle_id (&gm->rebuild_id, g_source_remove);
	if (gm->ui_manager != NULL) {
		g_signal_handlers_disconnect_by_data (gm->ui_manager, gm);
		g_clear_object (&gm->ui_manager);
	}
	g_signal_handlers_disconnect_by_data (nemo_preferences, gm);
	gm->window = NULL; /* se deja de tocar la ventana antes de soltar los ítems */
	g_hash_table_destroy (gm->actions);
	g_free (gm);
}

void
nemo_global_menu_install (NemoWindow *window)
{
	GlobalMenu *gm;

	g_return_if_fail (NEMO_IS_WINDOW (window));

	if (NEMO_IS_DESKTOP_WINDOW (window) || g_object_get_data (G_OBJECT (window), DATA_KEY) != NULL)
		return;

	gm = g_new0 (GlobalMenu, 1);
	gm->window = window;
	gm->ui_manager = g_object_ref (nemo_window_get_ui_manager (window));
	gm->actions = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, item_data_free);
	g_object_set_data_full (G_OBJECT (window), DATA_KEY, gm, global_menu_free);

	g_signal_connect_swapped (nemo_preferences, "changed::" NEMO_PREFERENCES_GLOBAL_MENU,
	                          G_CALLBACK (global_menu_pref_changed_cb), gm);
	g_signal_connect_swapped (window, "notify::is-active",
	                          G_CALLBACK (window_active_changed_cb), gm);
	g_signal_connect_swapped (window, "realize",
	                          G_CALLBACK (window_realized_cb), gm);
	g_signal_connect_swapped (gm->ui_manager, "actions-changed",
	                          G_CALLBACK (ui_changed_cb), gm);
	g_signal_connect_swapped (gm->ui_manager, "connect-proxy",
	                          G_CALLBACK (ui_changed_cb), gm);
	g_signal_connect_swapped (gm->ui_manager, "disconnect-proxy",
	                          G_CALLBACK (ui_changed_cb), gm);

	/* el menú se llena una vez que la ventana terminó de armarse */
	if (nemo_global_menu_hides_menubar ())
		schedule_rebuild (gm);
	sync_publication (gm);
}
