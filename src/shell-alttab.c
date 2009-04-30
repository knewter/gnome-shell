/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#include "shell-alttab.h"
#include "shell-global.h"
#include "shell-wm.h"
#include <mutter-plugin.h>

G_DEFINE_TYPE (ShellAltTabHandler, shell_alt_tab_handler, META_TYPE_ALT_TAB_HANDLER);

/* Signals */
enum
{
  WINDOW_ADDED,
  SHOW,
  HIDE,

  LAST_SIGNAL
};
static guint signals [LAST_SIGNAL] = { 0 };

static void
shell_alt_tab_handler_init (ShellAltTabHandler *sth)
{
  sth->windows = g_ptr_array_new ();
  sth->selected = -1;
}

static void
shell_alt_tab_handler_constructed (GObject *object)
{
  ShellGlobal *global = shell_global_get ();
  ShellWM *wm;

  g_object_get (G_OBJECT (global), "window-manager", &wm, NULL);
  _shell_wm_begin_alt_tab (wm, META_ALT_TAB_HANDLER (object));
  g_object_unref (wm);
}

static void
shell_alt_tab_handler_finalize (GObject *object)
{
  ShellAltTabHandler *sth = SHELL_ALT_TAB_HANDLER (object);

  g_ptr_array_free (sth->windows, FALSE);

  G_OBJECT_CLASS (shell_alt_tab_handler_parent_class)->finalize (object);
}

static void
shell_alt_tab_handler_add_window (MetaAltTabHandler *handler,
				  MetaWindow        *window)
{
  ShellAltTabHandler *sth = SHELL_ALT_TAB_HANDLER (handler);

  g_ptr_array_add (sth->windows, window);
  g_signal_emit (handler, signals[WINDOW_ADDED], 0,
                 meta_window_get_compositor_private (window));
}

static void
shell_alt_tab_handler_show (MetaAltTabHandler *handler)
{
  g_signal_emit (handler, signals[SHOW], 0);
}

static void
shell_alt_tab_handler_hide (MetaAltTabHandler *handler)
{
  g_signal_emit (handler, signals[HIDE], 0);
}

static void
shell_alt_tab_handler_select (MetaAltTabHandler *handler,
                              MetaWindow        *window)
{
  ShellAltTabHandler *sth = SHELL_ALT_TAB_HANDLER (handler);
  int i;

  sth->selected = -1;
  for (i = 0; i < sth->windows->len; i++)
    {
      if (sth->windows->pdata[i] == (gpointer)window)
        {
          sth->selected = i;
          break;
        }
    }

  /* Don't need a signal here; use notify::selected. */
}

static void
shell_alt_tab_handler_forward (MetaAltTabHandler *handler)
{
  ShellAltTabHandler *sth = SHELL_ALT_TAB_HANDLER (handler);

  sth->selected = (sth->selected + 1) % sth->windows->len;
  g_object_notify (G_OBJECT (handler), "selected");
}

static void
shell_alt_tab_handler_backward (MetaAltTabHandler *handler)
{
  ShellAltTabHandler *sth = SHELL_ALT_TAB_HANDLER (handler);

  sth->selected = (sth->selected - 1) % sth->windows->len;
  g_object_notify (G_OBJECT (handler), "selected");
}

static MetaWindow *
shell_alt_tab_handler_get_selected (MetaAltTabHandler *handler)
{
  ShellAltTabHandler *sth = SHELL_ALT_TAB_HANDLER (handler);

  if (sth->selected > -1)
    return sth->windows->pdata[sth->selected];
  else
    return NULL;
}

static void
shell_alt_tab_handler_class_init (ShellAltTabHandlerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaAltTabHandlerClass *handler_class = META_ALT_TAB_HANDLER_CLASS (klass);

  object_class->constructed   = shell_alt_tab_handler_constructed;
  object_class->finalize      = shell_alt_tab_handler_finalize;

  handler_class->add_window   = shell_alt_tab_handler_add_window;
  handler_class->show         = shell_alt_tab_handler_show;
  handler_class->hide         = shell_alt_tab_handler_hide;
  handler_class->select       = shell_alt_tab_handler_select;
  handler_class->forward      = shell_alt_tab_handler_forward;
  handler_class->backward     = shell_alt_tab_handler_backward;
  handler_class->get_selected = shell_alt_tab_handler_get_selected;

  signals[WINDOW_ADDED] = g_signal_new ("window-added",
                                        G_TYPE_FROM_CLASS (klass),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL, NULL,
                                        g_cclosure_marshal_VOID__OBJECT,
                                        G_TYPE_NONE, 1,
                                        MUTTER_TYPE_COMP_WINDOW);
  signals[SHOW] = g_signal_new ("show",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                g_cclosure_marshal_VOID__VOID,
                                G_TYPE_NONE, 0);
  signals[HIDE] = g_signal_new ("hide",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                g_cclosure_marshal_VOID__VOID,
                                G_TYPE_NONE, 0);
}
