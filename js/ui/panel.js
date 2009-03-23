/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Big = imports.gi.Big;
const Clutter = imports.gi.Clutter;
const Mainloop = imports.mainloop;
const Meta = imports.gi.Meta;
const Shell = imports.gi.Shell;

const Button = imports.ui.button;
const Main = imports.ui.main;

const PANEL_HEIGHT = 32;
const TRAY_HEIGHT = 28;
const SHADOW_HEIGHT = 6;

// The panel has a transparent white background with a gradient.
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

// Darken (pressed) buttons; lightening has no effect on white backgrounds.
const PANEL_BUTTON_COLOR = new Clutter.Color();
PANEL_BUTTON_COLOR.from_pixel(0x00000015);
const PRESSED_BUTTON_BACKGROUND_COLOR = new Clutter.Color();
PRESSED_BUTTON_BACKGROUND_COLOR.from_pixel(0x00000030);

const TRAY_BACKGROUND_COLOR = new Clutter.Color();
TRAY_BACKGROUND_COLOR.from_pixel(0xefefefff);
const TRAY_BORDER_COLOR = new Clutter.Color();
TRAY_BORDER_COLOR.from_pixel(0x00000033);
const TRAY_CORNER_RADIUS = 5;
const TRAY_BORDER_WIDTH = 1;
const TRAY_PADDING = 2;
const TRAY_SPACING = 2;

function Panel() {
    this._init();
}

Panel.prototype = {
    _init : function() {
        let me = this;
        let global = Shell.Global.get();

        // Put the background under the panel within a group.
        let group = new Clutter.Group();

        // Create a box with the background and the shadow.
        let backBox = new Big.Box({ orientation: Big.BoxOrientation.VERTICAL,
                                    x: 0,
                                    y: 0,
                                    width: global.screen_width,
                                    height: PANEL_HEIGHT + SHADOW_HEIGHT });
        let backUpper = global.create_vertical_gradient(PANEL_TOP_COLOR,
                                                        PANEL_MIDDLE_COLOR);
        let backLower = global.create_vertical_gradient(PANEL_MIDDLE_COLOR,
                                                        PANEL_BOTTOM_COLOR);
        let shadow    = global.create_vertical_gradient(SHADOW_COLOR,
                                                        TRANSPARENT_COLOR);
        shadow.set_height(SHADOW_HEIGHT);
        backBox.append(backUpper, Big.BoxPackFlags.EXPAND);
        backBox.append(backLower, Big.BoxPackFlags.EXPAND);
        backBox.append(shadow,    Big.BoxPackFlags.NONE);
        group.add_actor(backBox);

        this._box = new Big.Box({ background_color: TRANSPARENT_COLOR,
                                  x: 0,
                                  y: 0,
                                  height: PANEL_HEIGHT,
                                  width: global.screen_width,
                                  orientation: Big.BoxOrientation.HORIZONTAL,
                                  spacing: 4 });

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
                                     padding_left: 4,
                                     padding_right: 4 });
        clockbox.append(this._clock, Big.BoxPackFlags.NONE);
        this._box.append(clockbox, Big.BoxPackFlags.END);

        // The tray icons live in trayBox within trayContainer.
        // With Gtk 2.16, we can also use a transparent background for this.
        // The trayBox is hidden when there are no tray icons.
        let trayContainer = new Big.Box({ orientation: Big.BoxOrientation.VERTICAL,
                                          y_align: Big.BoxAlignment.CENTER });
        this._box.append(trayContainer, Big.BoxPackFlags.END);
        let trayBox = new Big.Box({ orientation: Big.BoxOrientation.HORIZONTAL,
                                    height: TRAY_HEIGHT,
                                    background_color: TRAY_BACKGROUND_COLOR,
                                    corner_radius: TRAY_CORNER_RADIUS,
                                    border: TRAY_BORDER_WIDTH,
                                    border_color: TRAY_BORDER_COLOR,
                                    padding: TRAY_PADDING,
                                    spacing: TRAY_SPACING });
        trayBox.hide();
        trayContainer.append(trayBox, Big.BoxPackFlags.NONE);

        this._traymanager = new Shell.TrayManager({ bg_color: TRAY_BACKGROUND_COLOR });
        this._traymanager.connect('tray-icon-added',
            function(o, icon) {
                trayBox.append(icon, Big.BoxPackFlags.NONE);

                // Make sure the trayBox is shown.
                trayBox.show();
            });
        this._traymanager.connect('tray-icon-removed',
            function(o, icon) {
                trayBox.remove_actor(icon);

                if (trayBox.get_children().length == 0)
                    trayBox.hide();
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

        group.add_actor(this._box);

        global.stage.add_actor(group);

        global.screen.connect('restacked',
            function() {
                me._restacked();
            });
        this._restacked();

        // Start the clock
        this._updateClock();
    },

    set_stage_input_area: function() {
        let global = Shell.Global.get();

        if (this._box.visible) {
            global.set_stage_input_area(this._box.x, this._box.y,
                                        this._box.width, this._box.height);
        } else
            global.set_stage_input_area(0, 0, 0, 0);
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
                side: Meta.Side.TOP
            })
        ];

        let screen = global.screen;
        for (let i = 0; i < screen.n_workspaces; i++) {
            let workspace = screen.get_workspace_by_index(i);
            workspace.set_builtin_struts(struts);
        }
    },

    _restacked: function() {
        let global = Shell.Global.get();
        let windows = global.get_windows();
        let i;

        // We want to be visible unless there is a window with layer
        // FULLSCREEN, or a window with layer OVERRIDE_REDIRECT that
        // completely covers us. (We can't set a non-rectangular
        // stage_input_area, so we don't let windows overlap us
        // partially.). "override_redirect" is not actually a layer
        // above all other windows, but this seems to be how mutter
        // treats it currently...
        //
        // @windows is sorted bottom to top.
        this._box.show();
        for (i = windows.length - 1; i > -1; i--) {
            let layer = windows[i].get_meta_window().get_layer();

            if (layer == Meta.StackLayer.OVERRIDE_REDIRECT) {
                if (windows[i].x <= this._box.x &&
                    windows[i].x + windows[i].width >= this._box.x + this._box.width &&
                    windows[i].y <= this._box.y &&
                    windows[i].y + windows[i].height >= this._box.y + this._box.height) {
                    this._box.hide();
                    break;
                }
            } else if (layer == Meta.StackLayer.FULLSCREEN) {
                this._box.hide();
                break;
            } else
                break;
        }

        this.set_stage_input_area();
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
