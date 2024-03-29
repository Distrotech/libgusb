/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>
#include <gusb/gusb.h>
#include <string.h>

/**
 * gusb_log_ignore_cb:
 **/
static void
gusb_log_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		   const gchar *message, gpointer user_data)
{
}

/**
 * gusb_log_handler_cb:
 **/
static void
gusb_log_handler_cb (const gchar *log_domain, GLogLevelFlags log_level,
		    const gchar *message, gpointer user_data)
{
	gchar str_time[255];
	time_t the_time;

	/* header always in green */
	time (&the_time);
	strftime (str_time, 254, "%H:%M:%S", localtime (&the_time));
	g_print ("%c[%dmTI:%s\t", 0x1B, 32, str_time);

	/* critical is also in red */
	if (log_level == G_LOG_LEVEL_CRITICAL ||
	    log_level == G_LOG_LEVEL_WARNING ||
	    log_level == G_LOG_LEVEL_ERROR) {
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, 31, message, 0x1B, 0);
	} else {
		/* debug in blue */
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, 34, message, 0x1B, 0);
	}
}

typedef struct {
	GOptionContext		*context;
	GUsbContext		*usb_ctx;
	GPtrArray		*cmd_array;
} GUsbCmdPrivate;

typedef gboolean (*GUsbCmdPrivateCb)	(GUsbCmdPrivate	*cmd,
					 gchar		**values,
					 GError		**error);

typedef struct {
	gchar			*name;
	gchar			*description;
	GUsbCmdPrivateCb	 callback;
} GUsbCmdItem;

/**
 * gusb_cmd_item_free:
 **/
static void
gusb_cmd_item_free (GUsbCmdItem *item)
{
	g_free (item->name);
	g_free (item->description);
	g_slice_free (GUsbCmdItem, item);
}

/*
 * gusb_sort_command_name_cb:
 */
static gint
gusb_sort_command_name_cb (GUsbCmdItem **item1, GUsbCmdItem **item2)
{
	return g_strcmp0 ((*item1)->name, (*item2)->name);
}

/**
 * gusb_cmd_add:
 **/
static void
gusb_cmd_add (GPtrArray *array, const gchar *name, const gchar *description, GUsbCmdPrivateCb callback)
{
	gchar **names;
	guint i;
	GUsbCmdItem *item;

	/* add each one */
	names = g_strsplit (name, ",", -1);
	for (i=0; names[i] != NULL; i++) {
		item = g_slice_new0 (GUsbCmdItem);
		item->name = g_strdup (names[i]);
		if (i == 0) {
			item->description = g_strdup (description);
		} else {
			/* TRANSLATORS: this is a command alias */
			item->description = g_strdup_printf ("Alias to %s",
							     names[0]);
		}
		item->callback = callback;
		g_ptr_array_add (array, item);
	}
	g_strfreev (names);
}

/**
 * gusb_cmd_get_descriptions:
 **/
static gchar *
gusb_cmd_get_descriptions (GPtrArray *array)
{
	guint i;
	guint j;
	guint len;
	guint max_len = 19;
	GUsbCmdItem *item;
	GString *string;

	/* print each command */
	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_string_append (string, "  ");
		g_string_append (string, item->name);
		g_string_append (string, " ");
		len = strlen (item->name);
		for (j=len; j<max_len+2; j++)
			g_string_append_c (string, ' ');
		g_string_append (string, item->description);
		g_string_append_c (string, '\n');
	}

	/* remove trailing newline */
	if (string->len > 0)
		g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

/**
 * gusb_device_list_added_cb:
 **/
static void
gusb_device_list_added_cb (GUsbDeviceList *list,
			   GUsbDevice *device,
			   gpointer user_data)
{
	g_print ("device %s added %x:%x\n",
		 g_usb_device_get_platform_id (device),
		 g_usb_device_get_bus (device),
		 g_usb_device_get_address (device));
}

/**
 * gusb_device_list_removed_cb:
 **/
static void
gusb_device_list_removed_cb (GUsbDeviceList *list,
			     GUsbDevice *device,
			     gpointer user_data)
{
	g_print ("device %s removed %x:%x\n",
		 g_usb_device_get_platform_id (device),
		 g_usb_device_get_bus (device),
		 g_usb_device_get_address (device));
}

/**
 * gusb_cmd_show:
 **/
static gboolean
gusb_cmd_show (GUsbCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = TRUE;
	GPtrArray *devices;
	guint i;
	GUsbDevice *device;
	GUsbDeviceList *list;

	list = g_usb_device_list_new (priv->usb_ctx);
	g_usb_device_list_coldplug (list);
	devices = g_usb_device_list_get_devices (list);
	for (i=0; i<devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		g_print ("device present %x:%x\n",
			 g_usb_device_get_bus (device),
			 g_usb_device_get_address (device));
	}
	g_ptr_array_unref (devices);
	if (list != NULL)
		g_object_unref (list);
	return ret;
}

/**
 * gusb_cmd_watch:
 **/
static gboolean
gusb_cmd_watch (GUsbCmdPrivate *priv, gchar **values, GError **error)
{
	gboolean ret = TRUE;
	GPtrArray *devices;
	guint i;
	GUsbDevice *device;
	GUsbDeviceList *list;
	GMainLoop *loop;

	list = g_usb_device_list_new (priv->usb_ctx);
	g_usb_device_list_coldplug (list);
	devices = g_usb_device_list_get_devices (list);
	for (i=0; i<devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		g_print ("device already present %x:%x\n",
			 g_usb_device_get_bus (device),
			 g_usb_device_get_address (device));
	}

	loop = g_main_loop_new (NULL, FALSE);
	g_signal_connect (list, "device-added",
			  G_CALLBACK (gusb_device_list_added_cb),
			  priv);
	g_signal_connect (list, "device-removed",
			  G_CALLBACK (gusb_device_list_removed_cb),
			  priv);
	g_main_loop_run (loop);

	g_main_loop_unref (loop);
	g_ptr_array_unref (devices);
	if (list != NULL)
		g_object_unref (list);
	return ret;
}

/**
 * gusb_cmd_run:
 **/
static gboolean
gusb_cmd_run (GUsbCmdPrivate *priv, const gchar *command, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	guint i;
	GUsbCmdItem *item;
	GString *string;

	/* find command */
	for (i=0; i<priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
		if (g_strcmp0 (item->name, command) == 0) {
			ret = item->callback (priv, values, error);
			goto out;
		}
	}

	/* not found */
	string = g_string_new ("");
	/* TRANSLATORS: error message */
	g_string_append_printf (string, "%s\n", "Command not found, valid commands are:");
	for (i=0; i<priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
		g_string_append_printf (string, " * %s\n", item->name);
	}
	g_set_error_literal (error, 1, 0, string->str);
	g_string_free (string, TRUE);
out:
	return ret;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean ret;
	gboolean verbose = FALSE;
	gchar *cmd_descriptions = NULL;
	gchar *options_help = NULL;
	GError *error = NULL;
	gint retval = 0;
	GUsbCmdPrivate *priv;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Show extra debugging information", NULL },
		{ NULL}
	};

	/* create helper object */
	priv = g_slice_new0 (GUsbCmdPrivate);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();

	priv->context = g_option_context_new ("GUSB Console Program");
	g_option_context_add_main_entries (priv->context, options, NULL);
	g_option_context_parse (priv->context, &argc, &argv, NULL);

	/* verbose? */
	if (verbose) {
		g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
		g_log_set_handler ("GUsb", G_LOG_LEVEL_ERROR |
					  G_LOG_LEVEL_CRITICAL |
					  G_LOG_LEVEL_DEBUG |
					  G_LOG_LEVEL_WARNING,
				   gusb_log_handler_cb, NULL);
	} else {
		/* hide all debugging */
		g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
		g_log_set_handler ("GUsb", G_LOG_LEVEL_DEBUG,
				   gusb_log_ignore_cb, NULL);
	}

	/* GUsbContext */
	priv->usb_ctx = g_usb_context_new (NULL);

	/* add commands */
	priv->cmd_array = g_ptr_array_new_with_free_func ((GDestroyNotify) gusb_cmd_item_free);
	gusb_cmd_add (priv->cmd_array,
		     "show",
		     "Show currently connected devices",
		     gusb_cmd_show);
	gusb_cmd_add (priv->cmd_array,
		     "watch",
		     "Watch devices as they come and go",
		     gusb_cmd_watch);

	/* sort by command name */
	g_ptr_array_sort (priv->cmd_array,
			  (GCompareFunc) gusb_sort_command_name_cb);

	/* get a list of the commands */
	cmd_descriptions = gusb_cmd_get_descriptions (priv->cmd_array);
	g_option_context_set_summary (priv->context, cmd_descriptions);

	/* nothing specified */
	if (argc < 2) {
		options_help = g_option_context_get_help (priv->context, TRUE, NULL);
		g_print ("%s", options_help);
		goto out;
	}

	/* run the specified command */
	ret = gusb_cmd_run (priv, argv[1], (gchar**) &argv[2], &error);
	if (!ret) {
		g_print ("%s\n", error->message);
		g_error_free (error);
		retval = 1;
		goto out;
	}
out:
	if (priv != NULL) {
		if (priv->usb_ctx != NULL)
			g_object_unref (priv->usb_ctx);
		g_option_context_free (priv->context);
		g_slice_free (GUsbCmdPrivate, priv);
	}

	/* free state */
	g_free (options_help);
	g_free (cmd_descriptions);
	return retval;
}
