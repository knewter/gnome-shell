/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Big = imports.gi.Big;
const Clutter = imports.gi.Clutter;
const Lang = imports.lang;
const Mainloop = imports.mainloop;
const Meta = imports.gi.Meta;
const Pango = imports.gi.Pango;
const Shell = imports.gi.Shell;
const Tidy = imports.gi.Tidy;

const Main = imports.ui.main;
const Overlay = imports.ui.overlay;
const Tweener = imports.ui.tweener;

const POPUP_BG_COLOR = new Clutter.Color();
POPUP_BG_COLOR.from_pixel(0x00000080);
const POPUP_INDICATOR_COLOR = new Clutter.Color();
POPUP_INDICATOR_COLOR.from_pixel(0xf0f0f0ff);
const POPUP_TRANSPARENT = new Clutter.Color();
POPUP_TRANSPARENT.from_pixel(0x00000000);

const RED = new Clutter.Color();
RED.from_pixel(0xff0000ff);
const GREEN = new Clutter.Color();
GREEN.from_pixel(0x00ff00ff);
const BLUE = new Clutter.Color();
BLUE.from_pixel(0x0000ffff);

const POPUP_INDICATOR_WIDTH = 4;
const POPUP_GRID_SPACING = 8;
const POPUP_ICON_SIZE = 48;
const POPUP_NUM_COLUMNS = 5;

const POPUP_LABEL_MAX_WIDTH = POPUP_NUM_COLUMNS * (POPUP_ICON_SIZE + POPUP_GRID_SPACING);

const OVERLAY_COLOR = new Clutter.Color();
OVERLAY_COLOR.from_pixel(0x00000044);

function AltTabPopup() {
    this._init();
}

AltTabPopup.prototype = {
    _init : function() {
	let global = Shell.Global.get();

	this.actor = new Big.Box({ background_color : POPUP_BG_COLOR,
                                   corner_radius: POPUP_GRID_SPACING,
                                   padding: POPUP_GRID_SPACING,
                                   spacing: POPUP_GRID_SPACING,
                                   orientation: Big.BoxOrientation.VERTICAL });

        // It would be nice to use Tidy.Grid for this, but it's lame
	this._grid = new Big.Box({ spacing: POPUP_GRID_SPACING,
                                   orientation: Big.BoxOrientation.VERTICAL });
        let gcenterbox = new Big.Box({ orientation: Big.BoxOrientation.HORIZONTAL,
                                       x_align: Big.BoxAlignment.CENTER });
        gcenterbox.append(this._grid, Big.BoxPackFlags.NONE);
	this.actor.append(gcenterbox, Big.BoxPackFlags.NONE);

	this._label = new Clutter.Text({ font_name: "Sans Bold 16px",
                                         ellipsize: Pango.EllipsizeMode.END });

        let labelbox = new Big.Box({ background_color: POPUP_INDICATOR_COLOR,
                                     corner_radius: POPUP_GRID_SPACING / 2,
                                     padding: POPUP_GRID_SPACING / 2 });
        // BigBox complains if you put an ellipsized ClutterText into it
        // directly! :-/
        let dummy = new Clutter.Group();
        dummy.add_actor(this._label);
        labelbox.append(dummy, Big.BoxPackFlags.EXPAND);
        let lcenterbox = new Big.Box({ orientation: Big.BoxOrientation.HORIZONTAL,
                                       x_align: Big.BoxAlignment.CENTER,
                                       width: POPUP_LABEL_MAX_WIDTH + POPUP_GRID_SPACING });
        lcenterbox.append(labelbox, Big.BoxPackFlags.NONE);
	this.actor.append(lcenterbox, Big.BoxPackFlags.NONE);

        this._indicator = new Big.Rectangle({ border_width: POPUP_INDICATOR_WIDTH,
                                              corner_radius: POPUP_INDICATOR_WIDTH / 2,
                                              border_color: POPUP_INDICATOR_COLOR,
                                              color: POPUP_TRANSPARENT });
        this.actor.append(this._indicator, Big.BoxPackFlags.FIXED);

	this._items = [];
        this._toplevels = global.window_group.get_children();

	global.stage.add_actor(this.actor);

        // FIXME: share code with runDialog.js
        this._overlay = new Clutter.Rectangle({ color: OVERLAY_COLOR,
                                                width: global.screen_width,
                                                height: global.screen_height,
                                                border_width: 0,
                                                reactive: true });
    },

    addWindow : function(win) {
        let item = { window: win,
                     metaWindow: win.get_meta_window() };

        let pixbuf = item.metaWindow.icon;
        item.icon = new Clutter.Texture({ width: Math.min(pixbuf.width, POPUP_ICON_SIZE),
                                          height: Math.min(pixbuf.height, POPUP_ICON_SIZE),
                                          keep_aspect_ratio: true });
        Shell.clutter_texture_set_from_pixbuf(item.icon, pixbuf);

        item.box = new Big.Box({ padding: POPUP_INDICATOR_WIDTH * 2 });
        item.box.append(item.icon, Big.BoxPackFlags.NONE);

        item.above = null;
        for (let i = 1; i < this._toplevels.length; i++) {
            if (this._toplevels[i] == win) {
                item.above = this._toplevels[i - 1];
                break;
            }
        }

        this._items.push(item);

        // Add it to the grid
        if (!this._gridRow || this._gridRow.get_children().length == POPUP_NUM_COLUMNS) {
            this._gridRow = new Big.Box({ spacing: POPUP_GRID_SPACING,
                                          orientation: Big.BoxOrientation.HORIZONTAL });
            this._grid.append(this._gridRow, Big.BoxPackFlags.NONE);
        }
        this._gridRow.append(item.box, Big.BoxPackFlags.NONE);
    },

    show : function() {
	let global = Shell.Global.get();

        Main.startModal();

        global.window_group.add_actor(this._overlay);
        this._overlay.raise_top();
        this._overlay.show();

	this.actor.show_all();
        this.actor.x = (global.screen_width - this.actor.width) / 2;
        this.actor.y = (global.screen_height - this.actor.height) / 2;
    },

    hide : function() {
	this.actor.hide();
        this._overlay.hide();
        this._overlay.unparent();

        Main.endModal();
    },

    select : function(window) {
        if (this._selected) {
            // Unselect previous

            if (this._allocationChangedId) {
                this._selected.box.disconnect(this._allocationChangedId);
                delete this._allocationChangedId;
            }

            if (this._selected.above)
                this._selected.window.raise(this._selected.above);
            else
                this._selected.window.lower_bottom();
        }

        let item;
        for (let i = 0; i < this._items.length; i++) {
            if (this._items[i].window == window) {
                item = this._items[i];
                break;
            }
        }

        let changed = this._selected && item != this._selected;
        this._selected = item;

        if (this._selected) {
            this._label.set_size(-1, -1);
            this._label.text = this._selected.metaWindow.title;
            if (this._label.width > POPUP_LABEL_MAX_WIDTH)
                this._label.width = POPUP_LABEL_MAX_WIDTH;

            // Figure out this._selected.box's coordinates in terms of
            // this.actor
            let bx = this._selected.box.x, by = this._selected.box.y;
            let actor = this._selected.box.get_parent();
            while (actor != this.actor) {
                bx += actor.x;
                by += actor.y;
                actor = actor.get_parent();
            }

            if (changed) {
                Tweener.addTween(this._indicator,
                                 { x: bx,
                                   y: by,
                                   width: this._selected.box.width,
                                   height: this._selected.box.height,
                                   time: Overlay.ANIMATION_TIME });
            } else {
                Tweener.removeTweens(this.indicator);
                this._indicator.set_position(bx, by);
                this._indicator.set_size(this._selected.box.width,
                                         this._selected.box.height);
            }
            this._indicator.show();

            if (this._overlay.visible)
                this._selected.window.raise(this._overlay);

            this._allocationChangedId =
                this._selected.box.connect('notify::allocation',
                                           Lang.bind(this, this._allocationChanged));
        } else {
            this._label.text = "";
            this._indicator.hide();
        }
    },

    _allocationChanged : function() {
        if (this._selected)
            this.select(this._selected.window);
    }
};
