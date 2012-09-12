/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 * nautilus-window-pane.c: Nautilus window pane
 *
 * Copyright (C) 2008 Free Software Foundation, Inc.
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Holger Berndt <berndth@gmx.de>
 *          Cosimo Cecchi <cosimoc@redhat.com>
 *
 */

#include "nautilus-window-pane.h"

#include "nautilus-actions.h"
#include "nautilus-location-bar.h"
#include "nautilus-notebook.h"
#include "nautilus-pathbar.h"
#include "nautilus-toolbar.h"
#include "nautilus-window-manage-views.h"
#include "nautilus-window-private.h"

#include <glib/gi18n.h>

#include <libnautilus-private/nautilus-clipboard.h>
#include <libnautilus-private/nautilus-global-preferences.h>
#include <libnautilus-private/nautilus-entry.h>

#define DEBUG_FLAG NAUTILUS_DEBUG_WINDOW
#include <libnautilus-private/nautilus-debug.h>

enum {
	PROP_WINDOW = 1,
	NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (NautilusWindowPane, nautilus_window_pane,
	       GTK_TYPE_BOX)

static void
unset_focus_widget (NautilusWindowPane *pane)
{
       if (pane->last_focus_widget != NULL) {
               g_object_remove_weak_pointer (G_OBJECT (pane->last_focus_widget),
                                             (gpointer *) &pane->last_focus_widget);
               pane->last_focus_widget = NULL;
       }
}

static inline NautilusWindowSlot *
get_first_inactive_slot (NautilusWindowPane *pane)
{
	GList *l;
	NautilusWindowSlot *slot;

	for (l = pane->slots; l != NULL; l = l->next) {
		slot = NAUTILUS_WINDOW_SLOT (l->data);
		if (slot != pane->active_slot) {
			return slot;
		}
	}

	return NULL;
}

static void
notebook_popup_menu_new_tab_cb (GtkMenuItem *menuitem,
				gpointer user_data)
{
	NautilusWindowPane *pane;

	pane = user_data;
	nautilus_window_new_tab (pane->window);
}

static void
notebook_popup_menu_move_left_cb (GtkMenuItem *menuitem,
				  gpointer user_data)
{
	NautilusWindowPane *pane;

	pane = NAUTILUS_WINDOW_PANE (user_data);
	nautilus_notebook_reorder_current_child_relative (NAUTILUS_NOTEBOOK (pane->notebook), -1);
}

static void
notebook_popup_menu_move_right_cb (GtkMenuItem *menuitem,
				   gpointer user_data)
{
	NautilusWindowPane *pane;

	pane = NAUTILUS_WINDOW_PANE (user_data);
	nautilus_notebook_reorder_current_child_relative (NAUTILUS_NOTEBOOK (pane->notebook), 1);
}

static void
notebook_popup_menu_close_cb (GtkMenuItem *menuitem,
			      gpointer user_data)
{
	NautilusWindowPane *pane;
	NautilusWindowSlot *slot;

	pane = NAUTILUS_WINDOW_PANE (user_data);
	slot = pane->active_slot;
	nautilus_window_pane_slot_close (pane, slot);
}

static void
notebook_popup_menu_show (NautilusWindowPane *pane,
			  GdkEventButton *event)
{
	GtkWidget *popup;
	GtkWidget *item;
	GtkWidget *image;
	int button, event_time;
	gboolean can_move_left, can_move_right;
	NautilusNotebook *notebook;

	notebook = NAUTILUS_NOTEBOOK (pane->notebook);

	can_move_left = nautilus_notebook_can_reorder_current_child_relative (notebook, -1);
	can_move_right = nautilus_notebook_can_reorder_current_child_relative (notebook, 1);

	popup = gtk_menu_new();

	item = gtk_menu_item_new_with_mnemonic (_("_New Tab"));
	g_signal_connect (item, "activate",
			  G_CALLBACK (notebook_popup_menu_new_tab_cb),
			  pane);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup),
			       item);

	gtk_menu_shell_append (GTK_MENU_SHELL (popup),
			       gtk_separator_menu_item_new ());

	item = gtk_menu_item_new_with_mnemonic (_("Move Tab _Left"));
	g_signal_connect (item, "activate",
			  G_CALLBACK (notebook_popup_menu_move_left_cb),
			  pane);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup),
			       item);
	gtk_widget_set_sensitive (item, can_move_left);

	item = gtk_menu_item_new_with_mnemonic (_("Move Tab _Right"));
	g_signal_connect (item, "activate",
			  G_CALLBACK (notebook_popup_menu_move_right_cb),
			  pane);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup),
			       item);
	gtk_widget_set_sensitive (item, can_move_right);

	gtk_menu_shell_append (GTK_MENU_SHELL (popup),
			       gtk_separator_menu_item_new ());

	item = gtk_image_menu_item_new_with_mnemonic (_("_Close Tab"));
	image = gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (item, "activate",
			  G_CALLBACK (notebook_popup_menu_close_cb), pane);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup),
			       item);

	gtk_widget_show_all (popup);

	if (event) {
		button = event->button;
		event_time = event->time;
	} else {
		button = 0;
		event_time = gtk_get_current_event_time ();
	}

	/* TODO is this correct? */
	gtk_menu_attach_to_widget (GTK_MENU (popup),
				   pane->notebook,
				   NULL);

	gtk_menu_popup (GTK_MENU (popup), NULL, NULL, NULL, NULL,
			button, event_time);
}

/* emitted when the user clicks the "close" button of tabs */
static void
notebook_tab_close_requested (NautilusNotebook *notebook,
			      NautilusWindowSlot *slot,
			      NautilusWindowPane *pane)
{
	nautilus_window_pane_slot_close (pane, slot);
}

static gboolean
notebook_button_press_cb (GtkWidget *widget,
			  GdkEventButton *event,
			  gpointer user_data)
{
	NautilusWindowPane *pane;

	pane = user_data;
	if (GDK_BUTTON_PRESS == event->type && 3 == event->button) {
		notebook_popup_menu_show (pane, event);
		return TRUE;
	}

	return FALSE;
}

static gboolean
notebook_popup_menu_cb (GtkWidget *widget,
			gpointer user_data)
{
	NautilusWindowPane *pane;

	pane = user_data;
	notebook_popup_menu_show (pane, NULL);
	return TRUE;
}

static gboolean
notebook_switch_page_cb (GtkNotebook *notebook,
			 GtkWidget *page,
			 unsigned int page_num,
			 NautilusWindowPane *pane)
{
	NautilusWindowSlot *slot;
	GtkWidget *widget;

	widget = gtk_notebook_get_nth_page (GTK_NOTEBOOK (pane->notebook), page_num);
	g_assert (widget != NULL);

	/* find slot corresponding to the target page */
	slot = NAUTILUS_WINDOW_SLOT (widget);
	g_assert (slot != NULL);

	nautilus_window_set_active_slot (nautilus_window_slot_get_window (slot),
					 slot);

	return FALSE;
}

static void
nautilus_window_pane_set_property (GObject *object,
				   guint arg_id,
				   const GValue *value,
				   GParamSpec *pspec)
{
	NautilusWindowPane *self = NAUTILUS_WINDOW_PANE (object);

	switch (arg_id) {
	case PROP_WINDOW:
		self->window = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, arg_id, pspec);
		break;
	}
}

static void
nautilus_window_pane_get_property (GObject *object,
				   guint arg_id,
				   GValue *value,
				   GParamSpec *pspec)
{
	NautilusWindowPane *self = NAUTILUS_WINDOW_PANE (object);

	switch (arg_id) {
	case PROP_WINDOW:
		g_value_set_object (value, self->window);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, arg_id, pspec);
		break;
	}
}

static void
nautilus_window_pane_dispose (GObject *object)
{
	NautilusWindowPane *pane = NAUTILUS_WINDOW_PANE (object);

	unset_focus_widget (pane);

	pane->window = NULL;

	g_assert (pane->slots == NULL);

	G_OBJECT_CLASS (nautilus_window_pane_parent_class)->dispose (object);
}

static void
nautilus_window_pane_constructed (GObject *obj)
{
	NautilusWindowPane *pane = NAUTILUS_WINDOW_PANE (obj);
	GtkSizeGroup *header_size_group;
	NautilusWindow *window;

	G_OBJECT_CLASS (nautilus_window_pane_parent_class)->constructed (obj);

	window = pane->window;

	header_size_group = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);
	gtk_size_group_set_ignore_hidden (header_size_group, FALSE);

	pane->action_group = nautilus_window_get_main_action_group (window);

	/* start as non-active */
	nautilus_window_pane_set_active (pane, FALSE);

	/* initialize the notebook */
	pane->notebook = g_object_new (NAUTILUS_TYPE_NOTEBOOK, NULL);
	gtk_box_pack_start (GTK_BOX (pane), pane->notebook,
			    TRUE, TRUE, 0);
	g_signal_connect (pane->notebook,
			  "tab-close-request",
			  G_CALLBACK (notebook_tab_close_requested),
			  pane);
	g_signal_connect_after (pane->notebook,
				"button_press_event",
				G_CALLBACK (notebook_button_press_cb),
				pane);
	g_signal_connect (pane->notebook, "popup-menu",
			  G_CALLBACK (notebook_popup_menu_cb),
			  pane);
	g_signal_connect (pane->notebook,
			  "switch-page",
			  G_CALLBACK (notebook_switch_page_cb),
			  pane);

	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (pane->notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (pane->notebook), FALSE);
	gtk_widget_show (pane->notebook);
	gtk_container_set_border_width (GTK_CONTAINER (pane->notebook), 0);

	/* Ensure that the view has some minimal size and that other parts
	 * of the UI (like location bar and tabs) don't request more and
	 * thus affect the default position of the split view paned.
	 */
	gtk_widget_set_size_request (GTK_WIDGET (pane), 60, 60);

	/* we can unref the size group now */
	g_object_unref (header_size_group);
}

static void
nautilus_window_pane_class_init (NautilusWindowPaneClass *klass)
{
	GObjectClass *oclass = G_OBJECT_CLASS (klass);

	oclass->constructed = nautilus_window_pane_constructed;
	oclass->dispose = nautilus_window_pane_dispose;
	oclass->set_property = nautilus_window_pane_set_property;
	oclass->get_property = nautilus_window_pane_get_property;

	properties[PROP_WINDOW] =
		g_param_spec_object ("window",
				     "The NautilusWindow",
				     "The parent NautilusWindow",
				     NAUTILUS_TYPE_WINDOW,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (oclass, NUM_PROPERTIES, properties);
}

static void
nautilus_window_pane_init (NautilusWindowPane *pane)
{
	pane->slots = NULL;
	pane->active_slot = NULL;

	gtk_orientable_set_orientation (GTK_ORIENTABLE (pane), GTK_ORIENTATION_VERTICAL);
}

NautilusWindowPane *
nautilus_window_pane_new (NautilusWindow *window)
{
	return g_object_new (NAUTILUS_TYPE_WINDOW_PANE,
			     "window", window,
			     NULL);
}

void
nautilus_window_pane_set_active (NautilusWindowPane *pane,
				       gboolean is_active)
{
	GtkStyleContext *style;
	gboolean has_inactive;

	/* pane inactive style */
	style = gtk_widget_get_style_context (GTK_WIDGET (pane));
	has_inactive = gtk_style_context_has_class (style, "nautilus-inactive-pane");

	if (has_inactive == !is_active) {
		return;
	}

	if (is_active) {
		gtk_style_context_remove_class (style, "nautilus-inactive-pane");
	} else {
		gtk_style_context_add_class (style, "nautilus-inactive-pane");
	}

	gtk_widget_reset_style (GTK_WIDGET (pane));
}

void
nautilus_window_pane_slot_close (NautilusWindowPane *pane,
				 NautilusWindowSlot *slot)
{
	NautilusWindowSlot *next_slot;

	DEBUG ("Requesting to remove slot %p from pane %p", slot, pane);

	if (pane->window) {
		NautilusWindow *window;

		window = pane->window;

		if (pane->active_slot == slot) {
			next_slot = get_first_inactive_slot (NAUTILUS_WINDOW_PANE (pane));
			nautilus_window_set_active_slot (window, next_slot);
		}

		nautilus_window_pane_close_slot (pane, slot);

		/* If that was the last slot in the pane, close the pane or even the whole window. */
		if (pane->slots == NULL) {
			if (nautilus_window_split_view_showing (window)) {
				NautilusWindowPane *new_pane;

				DEBUG ("Last slot removed from the pane %p, closing it", pane);
				nautilus_window_close_pane (window, pane);

				new_pane = g_list_nth_data (window->details->panes, 0);

				if (new_pane->active_slot == NULL) {
					new_pane->active_slot = get_first_inactive_slot (new_pane);
				}

				DEBUG ("Calling set_active_pane, new slot %p", new_pane->active_slot);
				nautilus_window_set_active_pane (window, new_pane);
				nautilus_window_update_show_hide_menu_items (window);
			} else {
				DEBUG ("Last slot removed from the last pane, close the window");
				nautilus_window_close (window);
			}
		}
	}
}

void
nautilus_window_pane_grab_focus (NautilusWindowPane *pane)
{
	if (NAUTILUS_IS_WINDOW_PANE (pane) && pane->active_slot) {
		nautilus_view_grab_focus (pane->active_slot->content_view);
	}	
}

void
nautilus_window_pane_close_slot (NautilusWindowPane *pane,
				 NautilusWindowSlot *slot)
{
	int page_num;
	GtkNotebook *notebook;

	g_assert (NAUTILUS_IS_WINDOW_SLOT (slot));
	g_assert (NAUTILUS_IS_WINDOW_PANE (slot->pane));

	DEBUG ("Closing slot %p", slot);

	/* save pane because slot is not valid anymore after this call */
	pane = slot->pane;
	notebook = GTK_NOTEBOOK (pane->notebook);

	nautilus_window_manage_views_close_slot (slot);
	pane->slots = g_list_remove (pane->slots, slot);

	/* right now we can't send signal
	g_signal_emit (pane->window, signals[SLOT_REMOVED], 0, slot);*/

	page_num = gtk_notebook_page_num (notebook, GTK_WIDGET (slot));
	g_assert (page_num >= 0);

	g_signal_handlers_block_by_func (notebook,
					 G_CALLBACK (notebook_switch_page_cb),
					 pane);
	/* this will call gtk_widget_destroy on the slot */
	gtk_notebook_remove_page (notebook, page_num);
	g_signal_handlers_unblock_by_func (notebook,
					   G_CALLBACK (notebook_switch_page_cb),
					   pane);

	gtk_notebook_set_show_tabs (notebook,
				    gtk_notebook_get_n_pages (notebook) > 1);
}

NautilusWindowSlot *
nautilus_window_pane_open_slot (NautilusWindowPane *pane,
				NautilusWindowOpenSlotFlags flags)
{
	NautilusWindowSlot *slot;

	g_assert (NAUTILUS_IS_WINDOW_PANE (pane));
	g_assert (NAUTILUS_IS_WINDOW (pane->window));

	slot = nautilus_window_slot_new (pane);

	g_signal_handlers_block_by_func (pane->notebook,
					 G_CALLBACK (notebook_switch_page_cb),
					 pane);
	nautilus_notebook_add_tab (NAUTILUS_NOTEBOOK (pane->notebook),
				   slot,
				   (flags & NAUTILUS_WINDOW_OPEN_SLOT_APPEND) != 0 ?
				   -1 :
				   gtk_notebook_get_current_page (GTK_NOTEBOOK (pane->notebook)) + 1,
				   FALSE);
	g_signal_handlers_unblock_by_func (pane->notebook,
					   G_CALLBACK (notebook_switch_page_cb),
					   pane);

	pane->slots = g_list_append (pane->slots, slot);

	return slot;
}
