/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@users.sourceforge.net>.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* toolbar.c - for the button bars that go along the tops of windows */

#include "config.h"

#include <string.h>

#include "global.h"

#include "toolbar.h"
#include "options.h"
#include "support.h"
#include "main.h"
#include "menu.h"
#include "dnd.h"
#include "filer.h"
#include "pixmaps.h"
#include "bind.h"

typedef struct _Tool Tool;

typedef enum {DROP_NONE, DROP_TO_PARENT, DROP_TO_HOME} DropDest;

struct _Tool {
	guchar		*label;
	guchar		*name;
	guchar		*tip;		/* Tooltip */
	void		(*clicked)(GtkWidget *w, FilerWindow *filer_window);
	DropDest	drop_action;
	gboolean	enabled;
	MaskedPixmap	*icon;
	GtkWidget	**menu;		/* Right-click menu widget addr */
	GtkWidget	*option_button;	/* Button in the Options window */
};

/* Options bits */
static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();
static char *toolbar_type(char *data);
static char *toolbar_disable(char *data);

static OptionsSection options =
{
	N_("Toolbar options"),
	create_options,
	update_options,
	set_options,
	save_options
};

ToolbarType o_toolbar = TOOLBAR_NORMAL;

static GtkWidget *menu_toolbar;
static GtkTooltips *tooltips = NULL;

/* TRUE if the button presses (or released) should open a new window,
 * rather than reusing the existing one.
 */
#define NEW_WIN_BUTTON(button_event)	\
  (o_new_window_on_1 ? ((GdkEventButton *) button_event)->button == 1	\
		     : ((GdkEventButton *) button_event)->button != 1)

/* Static prototypes */
static void toolbar_close_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_up_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_home_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_help_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_refresh_clicked(GtkWidget *widget,
				    FilerWindow *filer_window);
static void toolbar_large_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_small_clicked(GtkWidget *widget, FilerWindow *filer_window);
static GtkWidget *add_button(GtkWidget *box, Tool *tool,
				FilerWindow *filer_window);
static GtkWidget *create_toolbar(FilerWindow *filer_window);
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    FilerWindow		*filer_window);
static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       FilerWindow	*filer_window);
static void handle_drops(FilerWindow *filer_window,
			 GtkWidget *button,
			 DropDest dest);
static void recreate_toolbar(FilerWindow *filer_window);

static Tool all_tools[] = {
	{N_("Close"), "close", N_("Close filer window"),
	 toolbar_close_clicked, DROP_NONE, FALSE,
	 NULL, NULL, NULL},
	 
	{N_("Up"), "up", N_("Change to parent directory"),
	 toolbar_up_clicked, DROP_TO_PARENT, TRUE,
	 NULL, NULL, NULL},
	 
	{N_("Home"), "home", N_("Change to home directory"),
	 toolbar_home_clicked, DROP_TO_HOME, TRUE,
	 NULL, NULL, NULL},
	
	{N_("Scan"), "refresh", N_("Rescan directory contents"),
	 toolbar_refresh_clicked, DROP_NONE, TRUE,
	 NULL, NULL, NULL},
	
	{N_("Large"), "large", N_("Display using large icons"),
	 toolbar_large_clicked, DROP_NONE, TRUE,
	 NULL, &display_large_menu, NULL},
	
	{N_("Small"), "small", N_("Display using small icons"),
	 toolbar_small_clicked, DROP_NONE, TRUE,
	 NULL, &display_small_menu, NULL},

	{N_("Help"), "help", N_("Show ROX-Filer help"),
	 toolbar_help_clicked, DROP_NONE, TRUE,
	 NULL, NULL, NULL},
};



/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void toolbar_init(void)
{
	int	i;
	
	options_sections = g_slist_prepend(options_sections, &options);
	option_register("toolbar_type", toolbar_type);
	option_register("toolbar_disable", toolbar_disable);

	tooltips = gtk_tooltips_new();

	for (i = 0; i < sizeof(all_tools) / sizeof(*all_tools); i++)
	{
		Tool	*tool = &all_tools[i];

		if (!tool->icon)
		{
			guchar	*path;

			path = g_strconcat("pixmaps/",
					tool->name, ".xpm", NULL);
			tool->icon = load_pixmap(path);
			g_free(path);
		}
	}
}

/* Create a new toolbar widget, suitable for adding to a filer window,
 * and return it.
 */
GtkWidget *toolbar_new(FilerWindow *filer_window)
{
	g_return_val_if_fail(filer_window != NULL, NULL);
	g_return_val_if_fail(o_toolbar != TOOLBAR_NONE, NULL);

	return create_toolbar(filer_window);
}



/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void toolbar_help_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	filer_opendir(make_path(app_dir, "Help")->str);
}

static void toolbar_refresh_clicked(GtkWidget *widget,
				    FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE &&
			((GdkEventButton *) event)->button != 1)
	{
		filer_opendir(filer_window->path);
	}
	else
	{
		full_refresh();
		filer_update_dir(filer_window, TRUE);
	}
}

static void toolbar_home_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE && NEW_WIN_BUTTON(event))
	{
		filer_opendir(home_dir);
	}
	else
		filer_change_to(filer_window, home_dir, NULL);
}

static void toolbar_close_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEvent	*event;

	g_return_if_fail(filer_window != NULL);

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE &&
			((GdkEventButton *) event)->button != 1)
	{
		filer_opendir(filer_window->path);
	}
	else
		gtk_widget_destroy(filer_window->window);
}

static void toolbar_up_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE && NEW_WIN_BUTTON(event))
	{
		filer_open_parent(filer_window);
	}
	else
		change_to_parent(filer_window);
}

static void toolbar_large_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	display_set_layout(filer_window, "Large");
}

static void toolbar_small_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	display_set_layout(filer_window, "Small");
}

static GtkWidget *create_toolbar(FilerWindow *filer_window)
{
	GtkWidget	*box;
	GtkWidget	*b;
	int		i;

	box = gtk_hbox_new(FALSE, 0);

	for (i = 0; i < sizeof(all_tools) / sizeof(*all_tools); i++)
	{
		Tool	*tool = &all_tools[i];

		if (!tool->enabled)
			continue;

		b = add_button(box, tool, filer_window);
		if (tool->drop_action != DROP_NONE)
			handle_drops(filer_window, b, tool->drop_action);
	}

	filer_window->toolbar_text = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(box), filer_window->toolbar_text,
			TRUE, TRUE, 4);

	return box;
}

/* This is used to simulate a click when button 3 is used (GtkButton
 * normally ignores this).
 */
static gint toolbar_other_button = 0;
static gint toolbar_adjust_pressed(GtkButton *button,
				GdkEventButton *event,
				FilerWindow *filer_window)
{
	gint	b = event->button;

	if ((b == 2 || b == 3) && toolbar_other_button == 0)
	{
		toolbar_other_button = event->button;
		gtk_grab_add(GTK_WIDGET(button));
		gtk_button_pressed(button);
	}

	return TRUE;
}

static gint toolbar_adjust_released(GtkButton *button,
				GdkEventButton *event,
				FilerWindow *filer_window)
{
	if (event->button == toolbar_other_button)
	{
		toolbar_other_button = 0;
		gtk_grab_remove(GTK_WIDGET(button));
		gtk_button_released(button);
	}

	return TRUE;
}

static gint menu_pressed(GtkWidget *button,
			 GdkEventButton *event,
			 FilerWindow *filer_window)
{
	GtkWidget	*menu;

	if (event->button != 3 && event->button != 2)
		return FALSE;

	menu = gtk_object_get_data(GTK_OBJECT(button), "popup_menu");
	g_return_val_if_fail(menu != NULL, TRUE);

	show_style_menu(filer_window, event, menu);

	return TRUE;
}

static GtkWidget *add_button(GtkWidget *box, Tool *tool,
				FilerWindow *filer_window)
{
	GtkWidget 	*button, *icon_widget;
	GtkSignalFunc	cb = GTK_SIGNAL_FUNC(tool->clicked);

	button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);

	if (tool->menu)
	{
		gtk_object_set_data(GTK_OBJECT(button), "popup_menu",
				*tool->menu);
		gtk_signal_connect(GTK_OBJECT(button), "button_press_event",
			GTK_SIGNAL_FUNC(menu_pressed), filer_window);
	}
	else
	{
		gtk_signal_connect(GTK_OBJECT(button), "button_press_event",
			GTK_SIGNAL_FUNC(toolbar_adjust_pressed), filer_window);
		gtk_signal_connect(GTK_OBJECT(button), "button_release_event",
			GTK_SIGNAL_FUNC(toolbar_adjust_released), filer_window);
	}

	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			cb, filer_window);

	gtk_tooltips_set_tip(tooltips, button, _(tool->tip), NULL);

	icon_widget = gtk_pixmap_new(tool->icon->pixmap, tool->icon->mask);

	if (o_toolbar == TOOLBAR_LARGE)
	{
		GtkWidget	*vbox, *text;

		vbox = gtk_vbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), icon_widget, TRUE, TRUE, 0);

		text = gtk_label_new(_(tool->label));
		gtk_box_pack_start(GTK_BOX(vbox), text, FALSE, TRUE, 0);

		gtk_container_add(GTK_CONTAINER(button), vbox);
	}
	else
		gtk_container_add(GTK_CONTAINER(button), icon_widget);

	gtk_container_set_border_width(GTK_CONTAINER(button), 1);
	gtk_misc_set_padding(GTK_MISC(icon_widget),
			o_toolbar == TOOLBAR_LARGE ? 16 : 8, 1);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, TRUE, 0);

	return button;
}

static void toggle_shaded(GtkWidget *widget)
{
	gtk_widget_set_sensitive(widget, !GTK_WIDGET_SENSITIVE(widget));
}

/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options(void)
{
	int		i;
	GtkWidget	*vbox, *menu, *hbox;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

	gtk_box_pack_start(GTK_BOX(vbox),
			gtk_label_new(_("Unshade the tools you want:")),
			FALSE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 4);

	for (i = 0; i < sizeof(all_tools) / sizeof(*all_tools); i++)
	{
		Tool		*tool = &all_tools[i];
		GtkWidget	*button;
		GtkWidget	*icon_widget, *vbox, *text;

		button = gtk_button_new();
		GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);
		gtk_tooltips_set_tip(tooltips, button, _(tool->tip), NULL);

		icon_widget = gtk_pixmap_new(tool->icon->pixmap,
					     tool->icon->mask);

		vbox = gtk_vbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), icon_widget, TRUE, TRUE, 0);

		text = gtk_label_new(_(tool->label));
		gtk_box_pack_start(GTK_BOX(vbox), text, FALSE, TRUE, 0);

		gtk_container_add(GTK_CONTAINER(button), vbox);

		gtk_container_set_border_width(GTK_CONTAINER(button), 1);
		gtk_misc_set_padding(GTK_MISC(icon_widget), 16, 1);
		gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);

		tool->option_button = button;

		gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(toggle_shaded), GTK_OBJECT(vbox));
	}

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(hbox),
			gtk_label_new(_("Toolbar type")),
			FALSE, TRUE, 0);
	menu_toolbar = gtk_option_menu_new();
	menu = gtk_menu_new();
	gtk_menu_append(GTK_MENU(menu),
			gtk_menu_item_new_with_label(_("None")));
	gtk_menu_append(GTK_MENU(menu),
			gtk_menu_item_new_with_label(_("Normal")));
	gtk_menu_append(GTK_MENU(menu),
			gtk_menu_item_new_with_label(_("Large")));
	gtk_option_menu_set_menu(GTK_OPTION_MENU(menu_toolbar), menu);
	gtk_box_pack_start(GTK_BOX(hbox), menu_toolbar, TRUE, TRUE, 0);

	return vbox;
}

/* Reflect current state by changing the widgets in the options box */
static void update_options()
{
	int	i;

	gtk_option_menu_set_history(GTK_OPTION_MENU(menu_toolbar), o_toolbar);

	for (i = 0; i < sizeof(all_tools) / sizeof(*all_tools); i++)
	{
		Tool		*tool = &all_tools[i];

		gtk_widget_set_sensitive(GTK_BIN(tool->option_button)->child,
				tool->enabled);
	}
}

/* Set current values by reading the states of the widgets in the options box */
static void set_options()
{
	GtkWidget 	*item, *menu;
	GList		*list;
	int		i;
	gboolean	changed = FALSE;
	ToolbarType	old_type = o_toolbar;
	
	menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(menu_toolbar));
	item = gtk_menu_get_active(GTK_MENU(menu));
	list = gtk_container_children(GTK_CONTAINER(menu));
	o_toolbar = (ToolbarType) g_list_index(list, item);
	g_list_free(list);

	for (i = 0; i < sizeof(all_tools) / sizeof(*all_tools); i++)
	{
		Tool		*tool = &all_tools[i];
		gboolean	old = tool->enabled;
		
		tool->enabled = GTK_WIDGET_SENSITIVE(
					GTK_BIN(tool->option_button)->child);
		if (tool->enabled != old)
			changed = TRUE;
	}

	if (changed || old_type != o_toolbar)
	{
		GList	*next;

		for (next = all_filer_windows; next; next = next->next)
		{
			FilerWindow *filer_window = (FilerWindow *) next->data;

			recreate_toolbar(filer_window);
		}
	}
}

static void save_options()
{
	GString	*tools;
	int	i;

	tools = g_string_new(NULL);

	option_write("toolbar_type", o_toolbar == TOOLBAR_NONE ? "None" :
				     o_toolbar == TOOLBAR_NORMAL ? "Normal" :
				     o_toolbar == TOOLBAR_LARGE ? "Large" :
				     "Unknown");

	for (i = 0; i < sizeof(all_tools) / sizeof(*all_tools); i++)
	{
		Tool		*tool = &all_tools[i];
		
		if (!tool->enabled)
		{
			if (tools->len)
				g_string_append(tools, ", ");
			g_string_append(tools, tool->name);
		}
	}

	option_write("toolbar_disable", tools->str);
	g_string_free(tools, TRUE);
}

static void disable_tool(char *name)
{
	int	i;

	for (i = 0; i < sizeof(all_tools) / sizeof(*all_tools); i++)
	{
		Tool	*tool = &all_tools[i];

		if (g_strcasecmp(tool->name, name) == 0)
		{
			tool->enabled = FALSE;
			return;
		}
	}
}

static char *toolbar_disable(char *data)
{
	int	i;
	char	*comma, *word;

	for (i = 0; i < sizeof(all_tools) / sizeof(*all_tools); i++)
	{
		Tool	*tool = &all_tools[i];

		tool->enabled = TRUE;
	}

	while (*data)
	{
		comma = strchr(data, ',');

		if (comma)
			word = g_strndup(data, comma - data);
		else
			word = g_strdup(data);

		g_strstrip(word);
		if (!*word)
			break;

		disable_tool(word);

		g_free(word);

		if (!comma)
			break;

		data = comma + 1;
	}

	return NULL;
}

static char *toolbar_type(char *data)
{
	if (g_strcasecmp(data, "None") == 0)
		o_toolbar = TOOLBAR_NONE;
	else if (g_strcasecmp(data, "Normal") == 0)
		o_toolbar = TOOLBAR_NORMAL;
	else if (g_strcasecmp(data, "Large") == 0)
		o_toolbar = TOOLBAR_LARGE;
	else
		return _("Unknown toolbar type");

	return NULL;
}

/* Called during the drag when the mouse is in a widget registered
 * as a drop target. Returns TRUE if we can accept the drop.
 */
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    FilerWindow		*filer_window)
{
	GdkDragAction	action = context->suggested_action;
	DropDest	dest;

	dest = (DropDest) gtk_object_get_data(GTK_OBJECT(widget),
							"toolbar_dest");

	if (dest == DROP_TO_HOME)
		g_dataset_set_data(context, "drop_dest_path", home_dir);
	else
	{
		guchar	*slash, *path;

		slash = strrchr(filer_window->path, '/');
		if (slash == NULL || slash == filer_window->path)
			path = g_strdup("/");
		else
			path = g_strndup(filer_window->path,
					slash - filer_window->path);
		g_dataset_set_data_full(context, "drop_dest_path",
						path, g_free);
	}
	
	g_dataset_set_data(context, "drop_dest_type", drop_dest_dir);
	gdk_drag_status(context, action, time);
	
	dnd_spring_load(context);
	gtk_button_set_relief(GTK_BUTTON(widget), GTK_RELIEF_NORMAL);

	return TRUE;
}

static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       FilerWindow	*filer_window)
{
	gtk_button_set_relief(GTK_BUTTON(widget), GTK_RELIEF_NONE);
	dnd_spring_abort();
}

static void handle_drops(FilerWindow *filer_window,
			 GtkWidget *button,
			 DropDest dest)
{
	make_drop_target(button, 0);
	gtk_signal_connect(GTK_OBJECT(button), "drag_motion",
			GTK_SIGNAL_FUNC(drag_motion), filer_window);
	gtk_signal_connect(GTK_OBJECT(button), "drag_leave",
			GTK_SIGNAL_FUNC(drag_leave), filer_window);
	gtk_object_set_data(GTK_OBJECT(button), "toolbar_dest",
			(gpointer) dest);
}

static void recreate_toolbar(FilerWindow *filer_window)
{
	GtkWidget	*frame = filer_window->toolbar_frame;
	
	if (GTK_BIN(frame)->child)
	{
		filer_window->toolbar_text = NULL;
		gtk_widget_destroy(((GtkBin *) frame)->child);
	}
	
	if (o_toolbar == TOOLBAR_NONE)
		gtk_widget_hide(frame);
	else
	{
		GtkWidget	*toolbar;

		toolbar = toolbar_new(filer_window);
		gtk_container_add(GTK_CONTAINER(filer_window->toolbar_frame),
				toolbar);
		gtk_widget_show_all(frame);
	}

	filer_target_mode(filer_window, NULL, NULL, NULL);
}