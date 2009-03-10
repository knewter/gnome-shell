/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Big = imports.gi.Big;
const Clutter = imports.gi.Clutter;
const Mainloop = imports.mainloop;
const Meta = imports.gi.Meta;
const Shell = imports.gi.Shell;

const Button = imports.ui.button;
const Main = imports.ui.main;

const PANEL_HEIGHT = 32;
const TRAY_HEIGHT = 24;
const SHADOW_HEIGHT = 6;
const PANEL_BACKGROUND_COLOR = new Clutter.Color();
PANEL_BACKGROUND_COLOR.from_pixel(0xeeddccff);
const PANEL_TOP_COLOR = new Clutter.Color();
PANEL_TOP_COLOR.from_pixel(0xffffff99);
const PANEL_MIDDLE_COLOR = new Clutter.Color();
PANEL_MIDDLE_COLOR.from_pixel(0xffffff88);
const PANEL_BOTTOM_COLOR = new Clutter.Color();
PANEL_BOTTOM_COLOR.from_pixel(0xffffffaa);
const SHADOW_COLOR = new Clutter.Color();
SHADOW_COLOR.from_pixel(0x00000033);
const TRANSPARENT_COLOR = new Clutter.Color();
TRANSPARENT_COLOR.from_pixel(0x00000000);
const PANEL_BUTTON_COLOR = new Clutter.Color();
PANEL_BUTTON_COLOR.from_pixel(0xccbbaa66);
const PRESSED_BUTTON_BACKGROUND_COLOR = new Clutter.Color();
PRESSED_BUTTON_BACKGROUND_COLOR.from_pixel(0xccbbaaff);

function Panel() {
    this._init();
}

Panel.prototype = {
    _init : function() {
        let me = this;
        let global = Shell.Global.get();

        this._box = new Big.Box({ background_color: TRANSPARENT_COLOR,
                                  x: 0,
                                  y: 0,
                                  height: PANEL_HEIGHT,
                                  width: global.screen_width,
                                  orientation: Big.BoxOrientation.HORIZONTAL,
                                  spacing: 4 });

        // Draw the panel background in two parts: the upper and the lower.
        let back_upper = new Clutter.CairoTexture({ surface_width: 1,
                                                    surface_height: 2 });
        back_upper.set_size(global.screen_width, PANEL_HEIGHT / 2);
        back_upper.set_position(0, 0);
        Shell.Global.draw_vertical_gradient(back_upper,
                                            PANEL_TOP_COLOR,
                                            PANEL_MIDDLE_COLOR);
        this._box.add_actor(back_upper);

        let back_lower = new Clutter.CairoTexture({ surface_width: 1,
                                                    surface_height: 2 });
        back_lower.set_size(global.screen_width, PANEL_HEIGHT / 2);
        back_lower.set_position(0, PANEL_HEIGHT / 2);
        Shell.Global.draw_vertical_gradient(back_lower,
                                            PANEL_MIDDLE_COLOR,
                                            PANEL_BOTTOM_COLOR);
        this._box.add_actor(back_lower);

        // Draw the panel's shadow.
        let shadow = new Clutter.CairoTexture({ surface_width: 1,
                                                surface_height: 2 });
        shadow.set_size(global.screen_width, 6);
        shadow.set_position(0, PANEL_HEIGHT + 1);
        Shell.Global.draw_vertical_gradient(shadow,
                                            SHADOW_COLOR,
                                            TRANSPARENT_COLOR);
        this._box.add_actor(shadow);

        this.button = new Button.Button("Activities", PANEL_BUTTON_COLOR, PRESSED_BUTTON_BACKGROUND_COLOR, true, null, PANEL_HEIGHT);

        this._box.append(this.button.button, Big.BoxPackFlags.NONE);

        let statusbox = new Big.Box();
        this._statusmenu = new Shell.StatusMenu();
        statusbox.append(this._statusmenu, Big.BoxPackFlags.NONE);
        let statusbutton = new Button.Button(statusbox, PANEL_BUTTON_COLOR, PRESSED_BUTTON_BACKGROUND_COLOR,
                                             true, null, PANEL_HEIGHT);
        statusbutton.button.connect('button-press-event', function (b, e) {
            me._statusmenu.toggle(e);
            return false;
        });
        this._box.append(statusbutton.button, Big.BoxPackFlags.END);
        // We get a deactivated event when the popup disappears
        this._statusmenu.connect('deactivated', function (sm) {
            statusbutton.release();
        });

        this._clock = new Clutter.Text({ font_name: "Sans Bold 16px",
                                         text: "" });
        let pad = (PANEL_HEIGHT - this._clock.height) / 2;
        let clockbox = new Big.Box({ padding_top: pad,
                                     padding_bottom: pad,
                                     padding_right: 4 });
        clockbox.append(this._clock, Big.BoxPackFlags.NONE);
        this._box.append(clockbox, Big.BoxPackFlags.END);

        this._traymanager = new Shell.TrayManager({ bg_color: PANEL_BACKGROUND_COLOR });
        this._traymanager.connect('tray-icon-added',
            function(o, icon) {
                let pad = (PANEL_HEIGHT - icon.height) / 2;
                icon._panel_box = new Big.Box({ padding_top: pad,
                                                padding_bottom: pad });
                icon._panel_box.append(icon, Big.BoxPackFlags.NONE);
                me._box.append(icon._panel_box, Big.BoxPackFlags.END);
            });
        this._traymanager.connect('tray-icon-removed',
            function(o, icon) {
                me._box.remove_actor(icon._panel_box);
            });
        this._traymanager.manage_stage(global.stage);

        // TODO: decide what to do with the rest of the panel in the overlay mode (make it fade-out, become non-reactive, etc.)
        // We get into the overlay mode on button-press-event as opposed to button-release-event because eventually we'll probably
        // have the overlay act like a menu that allows the user to release the mouse on the activity the user wants
        // to switch to.
        this.button.button.connect('button-press-event',
            function(o, event) {
                if (Main.overlay.visible)
                    Main.hide_overlay();
                else
                    Main.show_overlay();

                return true;
            });

        this._setStruts();
        global.screen.connect('notify::n-workspaces',
            function() {
                me._setStruts();
            });

        global.stage.add_actor(this._box);

        // Start the clock
        this._updateClock();
    },

    // Struts determine the area along each side of the screen that is reserved
    // and not available to applications
    _setStruts: function() {
        let global = Shell.Global.get();

        let struts = [
            new Meta.Strut({
                rect: {
                    x: 0,
                    y: 0,
                    width: global.screen_width,
                    height: PANEL_HEIGHT
                },
                side: Meta.Direction.TOP
            })
        ];

        let screen = global.screen;
        for (let i = 0; i < screen.n_workspaces; i++) {
            let workspace = screen.get_workspace_by_index(i);
            workspace.set_builtin_struts(struts);
        }
    },

    _updateClock: function() {
        let me = this;
        let display_date = new Date();
        let msec_remaining = 60000 - (1000 * display_date.getSeconds() +
                                      display_date.getMilliseconds());
        if (msec_remaining < 500) {
            display_date.setMinutes(display_date.getMinutes() + 1);
            msec_remaining += 60000;
        }
        this._clock.set_text(display_date.toLocaleFormat("%H:%M"));
        Mainloop.timeout_add(msec_remaining, function() {
            me._updateClock();
            return false;
        });
    },

    overlayHidden: function() {
        this.button.release();
    }
};
