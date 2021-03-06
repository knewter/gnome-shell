#ifndef __SHELL_WM_H__
#define __SHELL_WM_H__

#include <glib-object.h>
#include <mutter-plugin.h>

G_BEGIN_DECLS

typedef struct _ShellWM      ShellWM;
typedef struct _ShellWMClass ShellWMClass;

#define SHELL_TYPE_WM              (shell_wm_get_type ())
#define SHELL_WM(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), SHELL_TYPE_WM, ShellWM))
#define SHELL_WM_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), SHELL_TYPE_WM, ShellWMClass))
#define SHELL_IS_WM(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), SHELL_TYPE_WM))
#define SHELL_IS_WM_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), SHELL_TYPE_WM))
#define SHELL_WM_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), SHELL_TYPE_WM, ShellWMClass))

struct _ShellWMClass
{
  GObjectClass parent_class;

};

GType    shell_wm_get_type                    (void) G_GNUC_CONST;

ShellWM *shell_wm_new                         (MutterPlugin *plugin);

GList   *shell_wm_get_switch_workspace_actors (ShellWM      *wm);

void     shell_wm_completed_minimize          (ShellWM      *wm,
                                               MutterWindow *actor);
void     shell_wm_completed_maximize          (ShellWM      *wm,
                                               MutterWindow *actor);
void     shell_wm_completed_unmaximize        (ShellWM      *wm,
                                               MutterWindow *actor);
void     shell_wm_completed_map               (ShellWM      *wm,
                                               MutterWindow *actor);
void     shell_wm_completed_destroy           (ShellWM      *wm,
                                               MutterWindow *actor);
void     shell_wm_completed_switch_workspace  (ShellWM      *wm);


/* These forward along the different effects from GnomeShellPlugin */

void _shell_wm_minimize   (ShellWM      *wm,
			   MutterWindow *actor);
void _shell_wm_maximize   (ShellWM      *wm,
			   MutterWindow *actor,
			   gint          x,
			   gint          y,
			   gint          width,
			   gint          height);
void _shell_wm_unmaximize (ShellWM      *wm,
			   MutterWindow *actor,
			   gint          x,
			   gint          y,
			   gint          width,
			   gint          height);
void _shell_wm_map        (ShellWM      *wm,
			   MutterWindow *actor);
void _shell_wm_destroy    (ShellWM      *wm,
			   MutterWindow *actor);

void _shell_wm_switch_workspace (ShellWM              *wm,
				 const GList         **actors,
				 gint                  from,
				 gint                  to,
				 MetaMotionDirection   direction);
void _shell_wm_kill_effect      (ShellWM              *wm,
				 MutterWindow         *actor,
				 gulong                events);

G_END_DECLS

#endif /* __SHELL_WM_H__ */
