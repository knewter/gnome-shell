/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Big = imports.gi.Big;
const Clutter = imports.gi.Clutter;
const Lang = imports.lang;
const Meta = imports.gi.Meta;
const Pango = imports.gi.Pango;
const Shell = imports.gi.Shell;
const Signals = imports.signals;
const Wnck = imports.gi.Wnck;

const Main = imports.ui.main;
const Overlay = imports.ui.overlay;
const Panel = imports.ui.panel;
const Tweener = imports.ui.tweener;

const FONT = "Sans 16px";
const SEPARATOR_FONT = "Sans 20px";

const TEXT_COLOR = new Clutter.Color();
TEXT_COLOR.from_pixel(0x000000ff);

const ICON_SIZE = 18;

// No different mouseOver background color for now, because of leave-event
// issues.
const MOUSE_OVER_BACKGROUND_COLOR = new Clutter.Color();
MOUSE_OVER_BACKGROUND_COLOR.from_pixel(0x00000000);

const ACTIVE_BACKGROUND_COLOR = new Clutter.Color();
ACTIVE_BACKGROUND_COLOR.from_pixel(0x00000033);

function TrailBar() {
    this._init();
}

TrailBar.prototype = {
    _init : function() {
        let me = this;
        let global = Shell.Global.get();

        this._active = null;
        this._lastActive = null;

        this.actor = new Big.Box({ reactive: true,
                                   orientation: Big.BoxOrientation.HORIZONTAL,
                                   y_align: Big.BoxAlignment.FILL });

        this._indicator = new Big.Box({
                width: 0,
                height: Panel.PANEL_HEIGHT,
                background_color: ACTIVE_BACKGROUND_COLOR });
        this.actor.append(this._indicator, Big.BoxPackFlags.FIXED);

        this.activities = new Breadcrumb("Activities", this);
        this.activities.connect("activate", function() {
            Main.show_overlay();
            for each (let window in global.get_windows())
                if (window.get_window_type() == Meta.CompWindowType.NORMAL)
                    window.get_meta_window().unmaximize(Meta.MaximizeFlags.HORIZONTAL | Meta.MaximizeFlags.VERTICAL);
        });
        this.activities.connect("deactivate", Main.hide_overlay);
        this.actor.append(this.activities.button, Big.BoxPackFlags.EXPAND);

        this.actor.append(new Separator().actor, Big.BoxPackFlags.EXPAND);

        this.workspace = new Breadcrumb("Workspace", this);
        this.actor.append(this.workspace.button, Big.BoxPackFlags.EXPAND);

        this._indicator.set_x(this.workspace._label.get_x());

        Wnck.Screen.get_default().connect("active-workspace-changed",
            function(o, event) {
                me.workspace._label.set_text("Workspace " + (global.screen.get_active_workspace_index() + 1));
            });

        this.actor.append(new Separator().actor, Big.BoxPackFlags.EXPAND);

        this.window = new Breadcrumb("Window", this);
        this.window.connect("activate", function(o, e) {
            let window = global.screen.get_display().get_focus_window();
            if (window) {
                window.maximize(Meta.MaximizeFlags.HORIZONTAL | Meta.MaximizeFlags.VERTICAL);
                /*
                 for each (let window in global.get_windows())
                 if (window.get_window_type() == Meta.CompWindowType.NORMAL)
                 window.get_meta_window().maximize(Meta.MaximizeFlags.HORIZONTAL | Meta.MaximizeFlags.VERTICAL);
                 */
            }
        });
        this.actor.append(this.window.button, Big.BoxPackFlags.EXPAND);

        this.workspace.connect("activate", function() {
            let window = global.screen.get_display().get_focus_window();
            /*
             log(window.decorated);
             window.decorated = true;
             log(window.decorated);
             */
            if (window)
                window.unmaximize(Meta.MaximizeFlags.HORIZONTAL | Meta.MaximizeFlags.VERTICAL);
             /*
              for each (let window in global.get_windows())
                  if (window.get_window_type() == Meta.CompWindowType.NORMAL)
                      window.get_meta_window().unmaximize(Meta.MaximizeFlags.HORIZONTAL | Meta.MaximizeFlags.VERTICAL);
              */
        });

        this._breadcrumbs = [this.activities, this.workspace, this.window];

        global.screen.connect("restacked", Lang.bind(this, this._updateWindow));
        this.activate(this.workspace);
    },

    activate : function(breadcrumb) {
        if (this._active) {
            //if (this._active == breadcrumb)
            //    return;
            this._active.emit("deactivate");
            this._lastActive = this._active;
        }
        this._active = breadcrumb;
        Tweener.addTween(this._indicator, { x: breadcrumb._button.get_x(),
                                            width: breadcrumb._button.get_width(),
                                            time: Overlay.ANIMATION_TIME,
                                            transition: "easeOutQuad" });
        breadcrumb.emit("activate");
    },

    deactivate : function() {
        if (this._lastActive)
            this.activate(this._lastActive);
        else
            this.activate(this.activities);
    },

    _updateWindow : function(o, event) {
        let global = Shell.Global.get();
        let window = global.screen.get_display().get_focus_window();
        if (window) {
            this.window._label.set_text(window.title);

            let pixbuf = window./*mini_*/icon;
            Shell.clutter_texture_set_from_pixbuf(this.window._icon, pixbuf);
            this.window._icon.width = Math.min(ICON_SIZE, pixbuf.width);
            this.window._icon.height = Math.min(ICON_SIZE, pixbuf.height);
            this.window._iconBox.show();
        } else {
            this.window._label.set_text("(No windows)");
            this.window._iconBox.hide();
        }

        // Update indicator geometry.
        if (this._active == this.window) {
            this.activate(this.window);
        }
    }
};

function Separator() {
    this.actor = new Big.Box({ padding_left: 3,
                               padding_right: 3,
                               y_align: Big.BoxAlignment.CENTER });

    let label = new Clutter.Text({ font_name: SEPARATOR_FONT, text: "\u203a" });
    this.actor.append(label, Big.BoxPackFlags.EXPAND);
}

function Breadcrumb(label, trail) {
    this._init(label, trail);
}

Breadcrumb.prototype = {
    _init : function(label, trail) {
        let me = this;

        this._trail = trail;

        let staysPressed = false;
        let minWidth = 0;
        let minHeight = 0;

        // if staysPressed is true, this.active will be true past the first release of a button, untill a subsequent one (the button
        // is unpressed) or untill release() is called explicitly
        this._active = false;
        this._isBetweenPressAndRelease = false;
        this._mouseIsOverButton = false;

        this.button = new Big.Box({ reactive: true,
                                    padding_left: 6,
                                    padding_right: 6,
                                    orientation: Big.BoxOrientation.HORIZONTAL,
                                    y_align: Big.BoxAlignment.CENTER });
        this._button = this.button;

        this._iconBox = new Big.Box({ padding_right: 6,
                                      y_align: Big.BoxAlignment.CENTER });
        this.button.append(this._iconBox, Big.BoxPackFlags.NONE);
        this._iconBox.hide();

        this._icon = new Clutter.Texture({ width: ICON_SIZE,
                                           height: ICON_SIZE,
                                           keep_aspect_ratio: true });
        this._iconBox.append(this._icon, Big.BoxPackFlags.NONE);

        this._label = new Clutter.Text({ font_name: FONT,
                                         ellipsize: Pango.EllipsizeMode.MIDDLE,
                                         text: label });
        this._label.set_color(TEXT_COLOR);
        this.button.append(this._label, Big.BoxPackFlags.EXPAND);

        this._minWidth = minWidth;
        this._minHeight = minHeight;

        this.button.connect('button-press-event',
            function(o, event) {
                me._isBetweenPressAndRelease = true;
                //me._label.set_color(ACTIVE_TEXT_COLOR);
                me.activate();
                return false;
            });
        this.button.connect('button-release-event',
            function(o, event) {
                me._isBetweenPressAndRelease = false;
                if (!staysPressed || me._active) {
                    me.release();
                } else {
                    me._active = true;
                }
                return false;
            });
        this.button.connect('enter-event',
            function(o, event) {
                me._mouseIsOverButton = true;
                if (!me._active) {
                    me.button.backgroundColor = MOUSE_OVER_BACKGROUND_COLOR;
                }
                return false;
            });
        this.button.connect('leave-event',
            function(o, event) {
                me._isBetweenPressAndRelease = false;
                me._mouseIsOverButton = false;
                if (!me._active) {
                    me.button.backgroundColor = null;
                    //me._label.set_color(TEXT_COLOR);
                }
                return false;
            });
    },

    activate : function() {
        this._isBetweenPressAndRelease = false;
        this._active = true;
        //this.button.backgroundColor = ACTIVE_BACKGROUND_COLOR;
        //this._label.set_color(ACTIVE_TEXT_COLOR);
        this._trail.activate(this);
    },

    release : function() {
        if (!this._isBetweenPressAndRelease) {
            this._active = false;
            //this._label.set_color(TEXT_COLOR);
            if (this._mouseIsOverButton) {
                //this.button.backgroundColor = MOUSE_OVER_BACKGROUND_COLOR;
            } else {
                //this.button.backgroundColor = null;
            }
        }
    }
};

Signals.addSignalMethods(TrailBar.prototype);
Signals.addSignalMethods(Breadcrumb.prototype);
