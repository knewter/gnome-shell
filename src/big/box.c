/* -*- mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* big-box.c: Box container.

   Copyright (C) 2006-2008 Red Hat, Inc.
   Copyright (C) 2008 litl, LLC.

   The libbigwidgets-lgpl is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The libbigwidgets-lgpl is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the libbigwidgets-lgpl; see the file COPYING.LIB.
   If not, write to the Free Software Foundation, Inc., 59 Temple Place -
   Suite 330, Boston, MA 02111-1307, USA.
*/

#include <math.h>

#include <glib.h>

#include <clutter/clutter.h>
#include <cogl/cogl.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "big-enum-types.h"

#include "box.h"
#include "theme-image.h"
#include "rectangle.h"

static void clutter_container_iface_init (ClutterContainerIface *iface);

G_DEFINE_TYPE_WITH_CODE (BigBox,
                         big_box,
                         CLUTTER_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTAINER,
                                                clutter_container_iface_init));

#define BIG_BOX_GET_PRIVATE(obj)    \
(G_TYPE_INSTANCE_GET_PRIVATE ((obj), BIG_TYPE_BOX, BigBoxPrivate))

#define BOX_CHILD_IS_VISIBLE(c)    \
(CLUTTER_ACTOR_IS_VISIBLE (c->actor))

#define BOX_CHILD_IN_LAYOUT(c)    \
(!c->fixed && (BOX_CHILD_IS_VISIBLE (c) || c->if_hidden))

enum
{
  PROP_0,

  PROP_ORIENTATION,
  PROP_SPACING,
  PROP_X_ALIGN,
  PROP_Y_ALIGN,
  PROP_PADDING,
  PROP_PADDING_TOP,
  PROP_PADDING_BOTTOM,
  PROP_PADDING_LEFT,
  PROP_PADDING_RIGHT,
  PROP_BORDER,
  PROP_BORDER_TOP,
  PROP_BORDER_BOTTOM,
  PROP_BORDER_LEFT,
  PROP_BORDER_RIGHT,
  PROP_CORNER_RADIUS,
  PROP_BACKGROUND_BORDER_TOP,
  PROP_BACKGROUND_BORDER_BOTTOM,
  PROP_BACKGROUND_BORDER_LEFT,
  PROP_BACKGROUND_BORDER_RIGHT,
  PROP_BACKGROUND_COLOR,
  PROP_BACKGROUND_FILENAME,
  PROP_BACKGROUND_PIXBUF,
  PROP_BACKGROUND_TEXTURE,
  PROP_BACKGROUND_RECTANGLE,
  PROP_BACKGROUND_REPEAT,
  PROP_BACKGROUND_X_ALIGN,
  PROP_BACKGROUND_Y_ALIGN,
  PROP_BORDER_COLOR,
  PROP_DEBUG
};

struct _BigBoxPrivate
{
  GList                   *children;
  BigBoxOrientation        orientation;
  BigBoxAlignment          x_align;
  BigBoxAlignment          y_align;
  ClutterUnit              spacing;
  ClutterUnit              padding_top;
  ClutterUnit              padding_bottom;
  ClutterUnit              padding_left;
  ClutterUnit              padding_right;
  ClutterUnit              border_top;
  ClutterUnit              border_bottom;
  ClutterUnit              border_left;
  ClutterUnit              border_right;
  ClutterUnit              corner_radius;
  ClutterColor             border_color;
  guint                    background_border_top;
  guint                    background_border_bottom;
  guint                    background_border_left;
  guint                    background_border_right;
  ClutterColor             background_color;
  ClutterActor            *background_texture;
  BigBoxBackgroundRepeat   background_repeat;
  BigBoxAlignment          background_x_align;
  BigBoxAlignment          background_y_align;
  ClutterActor            *background_rectangle;

  guint                    draw_rounded_corner : 1;
  guint                    debug               : 1;
};

typedef struct
{
  ClutterActor *actor;

  guint         expand    : 1;
  guint         end       : 1;
  guint         if_fits   : 1;
  guint         fixed     : 1;
  guint         if_hidden : 1;

  /* BigBoxAlignment applies if fixed=true */
  guint         fixed_x_align : 3;
  guint         fixed_y_align : 3;
} BigBoxChild;

typedef struct
{
  ClutterUnit  minimum;
  ClutterUnit  natural;
  ClutterUnit  adjustment;
  guint        does_not_fit : 1;
} BigBoxAdjustInfo;

static gboolean
box_child_set_flags (BigBoxChild     *c,
                     BigBoxPackFlags  flags)
{
  BigBoxPackFlags old;

  old = 0;

  if (c->end)
    old |= BIG_BOX_PACK_END;
  if (c->expand)
    old |= BIG_BOX_PACK_EXPAND;
  if (c->if_fits)
    old |= BIG_BOX_PACK_IF_FITS;
  if (c->fixed)
    old |= BIG_BOX_PACK_FIXED;
  if (c->if_hidden)
    old |= BIG_BOX_PACK_ALLOCATE_WHEN_HIDDEN;

  if (old == flags)
    return FALSE; /* no change */

  c->expand    = (flags & BIG_BOX_PACK_EXPAND) != 0;
  c->end       = (flags & BIG_BOX_PACK_END) != 0;
  c->if_fits   = (flags & BIG_BOX_PACK_IF_FITS) != 0;
  c->fixed     = (flags & BIG_BOX_PACK_FIXED) != 0;
  c->if_hidden = (flags & BIG_BOX_PACK_ALLOCATE_WHEN_HIDDEN) != 0;

  return TRUE;
}

static gboolean
box_child_set_align (BigBoxChild     *c,
                     BigBoxAlignment  fixed_x_align,
                     BigBoxAlignment  fixed_y_align)
{
  if (fixed_x_align == c->fixed_x_align &&
      fixed_y_align == c->fixed_y_align)
    return FALSE;

  c->fixed_x_align = fixed_x_align;
  c->fixed_y_align = fixed_y_align;

  return TRUE;
}

static BigBoxChild *
box_child_find (BigBox       *box,
                ClutterActor *actor)
{
  GList *c;

  for (c = box->priv->children; c != NULL; c = c->next)
    {
      BigBoxChild *child = c->data;

      if (child->actor == actor)
        return (BigBoxChild *) child;
    }

  return NULL;
}

static BigBoxChild *
box_child_new_from_actor (ClutterActor    *child,
                          BigBoxPackFlags  flags)
{
  BigBoxChild *c;

  c = g_new0 (BigBoxChild, 1);

  g_object_ref (child);
  c->actor = child;

  box_child_set_flags (c, flags);

  return c;
}

static void
box_child_free (BigBoxChild *c)
{
  g_object_unref (c->actor);

  g_free (c);
}

static void
box_child_remove (BigBox *box, BigBoxChild *child)
{
  BigBoxPrivate *priv = box->priv;

  priv->children = g_list_remove (priv->children, child);

  clutter_actor_unparent (child->actor);

  /* at this point, the actor passed to the "actor-removed" signal
   * handlers is not parented anymore to the container but since we
   * are holding a reference on it, it's still valid
   */
  g_signal_emit_by_name (box, "actor-removed", child->actor);

  box_child_free (child);
}

static void
big_box_real_add (ClutterContainer *container,
                  ClutterActor     *child)
{
  big_box_append (BIG_BOX (container), child, BIG_BOX_PACK_FIXED);
}

static void
big_box_real_remove (ClutterContainer *container,
                     ClutterActor     *child)
{
  BigBox *box = BIG_BOX (container);
  BigBoxChild *c;

  g_object_ref (child);

  c = box_child_find (box, child);

  if (c != NULL)
    {
      box_child_remove (box, c);

      clutter_actor_queue_relayout (CLUTTER_ACTOR (box));
    }

  g_object_unref (child);
}

static void
big_box_real_foreach (ClutterContainer *container,
                      ClutterCallback   callback,
                      gpointer          user_data)
{
  BigBox *group = BIG_BOX (container);
  BigBoxPrivate *priv = group->priv;
  GList *l;

  for (l = priv->children; l; l = l->next)
   {
     BigBoxChild *c = (BigBoxChild *) l->data;

     (* callback) (c->actor, user_data);
   }
}

static void
big_box_real_raise (ClutterContainer *container,
                    ClutterActor     *child,
                    ClutterActor     *sibling)
{
  BigBox *box = BIG_BOX (container);
  BigBoxPrivate *priv = box->priv;
  BigBoxChild *c;

  c = box_child_find (box, child);

  /* Child not found */
  if (c == NULL)
    return;

  /* We only do raise for children not in the layout */
  if (!c->fixed)
    return;

  priv->children = g_list_remove (priv->children, c);

  /* Raise at the top */
  if (!sibling)
    {
      GList *last_item;

      last_item = g_list_last (priv->children);

      if (last_item)
        {
          BigBoxChild *sibling_child = last_item->data;

          sibling = sibling_child->actor;
        }

      priv->children = g_list_append (priv->children, c);
    }
  else
    {
      BigBoxChild *sibling_child;
      gint pos;

      sibling_child = box_child_find (box, sibling);

      pos = g_list_index (priv->children, sibling_child) + 1;

      priv->children = g_list_insert (priv->children, c, pos);
    }

  if (sibling &&
      clutter_actor_get_depth (sibling) != clutter_actor_get_depth (child))
    {
      clutter_actor_set_depth (child, clutter_actor_get_depth (sibling));
    }
}

static void
big_box_real_lower (ClutterContainer *container,
                    ClutterActor     *child,
                    ClutterActor     *sibling)
{
  BigBox *box = BIG_BOX (container);
  BigBoxPrivate *priv = box->priv;
  BigBoxChild *c;

  c = box_child_find (box, child);

  /* Child not found */
  if (c == NULL)
    return;

  /* We only do lower for children not in the layout */
  if (!c->fixed)
    return;

  priv->children = g_list_remove (priv->children, c);

  /* Push to bottom */
  if (!sibling)
    {
      GList *first_item;

      first_item = g_list_first (priv->children);

      if (first_item)
        {
          BigBoxChild *sibling_child = first_item->data;

          sibling = sibling_child->actor;
        }

      priv->children = g_list_prepend (priv->children, c);
    }
  else
    {
      BigBoxChild *sibling_child;
      gint pos;

      sibling_child = box_child_find (box, sibling);

      pos = g_list_index (priv->children, sibling_child);

      priv->children = g_list_insert (priv->children, c, pos);
    }

  if (sibling &&
      clutter_actor_get_depth (sibling) != clutter_actor_get_depth (child))
    {
      clutter_actor_set_depth (child, clutter_actor_get_depth (sibling));
    }
}

static int
sort_z_order (gconstpointer a,
              gconstpointer b)
{
  const BigBoxChild *child_a = a;
  const BigBoxChild *child_b = b;
  int depth_a, depth_b;

  /* Depth of non-fixed children is ignored in stacking/painting order and they
   * are considered to be at depth=0. Though when the children are painting
   * themselves the depth translation is applied.
   */

  depth_a = child_a->fixed ? clutter_actor_get_depth (child_a->actor) : 0;
  depth_b = child_b->fixed ? clutter_actor_get_depth (child_b->actor) : 0;

  return depth_a - depth_b;
}

static void
big_box_real_sort_depth_order (ClutterContainer *container)
{
  BigBox *box = BIG_BOX (container);
  BigBoxPrivate *priv = box->priv;

  priv->children = g_list_sort (priv->children, sort_z_order);

  if (CLUTTER_ACTOR_IS_VISIBLE (CLUTTER_ACTOR (box)))
    clutter_actor_queue_redraw (CLUTTER_ACTOR (box));
}

static void
clutter_container_iface_init (ClutterContainerIface *iface)
{
  iface->add = big_box_real_add;
  iface->remove = big_box_real_remove;
  iface->foreach = big_box_real_foreach;
  iface->raise = big_box_real_raise;
  iface->lower = big_box_real_lower;
  iface->sort_depth_order = big_box_real_sort_depth_order;
}

static void
big_box_get_bg_texture_allocation (ClutterActor          *self,
                                   ClutterUnit            allocated_width,
                                   ClutterUnit            allocated_height,
                                   ClutterActorBox       *bg_box)
{
  BigBoxPrivate *priv;
  ClutterUnit bg_width, bg_height;
  ClutterUnit min_x1, min_y1, max_x2, max_y2;

  g_return_if_fail (BIG_IS_BOX (self));

  priv = BIG_BOX (self)->priv;

  clutter_actor_get_preferred_width (priv->background_texture,
                                     -1,
                                     NULL,
                                     &bg_width);
  clutter_actor_get_preferred_height (priv->background_texture,
                                      bg_width,
                                      NULL,
                                      &bg_height);

  min_x1 = priv->border_left;
  max_x2 = allocated_width - priv->border_right;

  switch (priv->background_x_align)
    {
    case BIG_BOX_ALIGNMENT_FIXED:
      g_warning("Must specify a real alignment for background, not FIXED");
      break;
    case BIG_BOX_ALIGNMENT_FILL:
      bg_box->x1 = min_x1;
      bg_box->x2 = max_x2;
      break;

    case BIG_BOX_ALIGNMENT_START:
      bg_box->x1 = min_x1;
      bg_box->x2 = MIN (bg_box->x1 + bg_width, max_x2);
      break;

    case BIG_BOX_ALIGNMENT_END:
      bg_box->x1 = MAX (min_x1, max_x2 - bg_width);
      bg_box->x2 = max_x2;
      break;

    case BIG_BOX_ALIGNMENT_CENTER:
      bg_box->x1 = MAX (min_x1, priv->border_left + roundf ((allocated_width - bg_width) / 2));
      bg_box->x2 = MIN (bg_box->x1 + bg_width, max_x2);
      break;
    }

  min_y1 = priv->border_top;
  max_y2 = allocated_height - priv->border_bottom;

  switch (priv->background_y_align)
    {
    case BIG_BOX_ALIGNMENT_FIXED:
      g_warning("Must specify a real alignment for background, not FIXED");
      break;
    case BIG_BOX_ALIGNMENT_FILL:
      bg_box->y1 = min_y1;
      bg_box->y2 = max_y2;
      break;

    case BIG_BOX_ALIGNMENT_START:
      bg_box->y1 = min_y1;
      bg_box->y2 = MIN (bg_box->y1 + bg_height, max_y2);
      break;

    case BIG_BOX_ALIGNMENT_END:
      bg_box->y1 = MAX (min_y1, max_y2 - bg_height);
      bg_box->y2 = max_y2;
      break;

    case BIG_BOX_ALIGNMENT_CENTER:
      bg_box->y1 = MAX (min_y1, priv->border_top + roundf ((allocated_height - bg_height) / 2));
      bg_box->y2 = MIN (bg_box->y1 + bg_height, max_y2);
      break;
    }
}

static void
big_box_update_background_border (BigBox *box)
{
  BigBoxPrivate *priv;

  g_return_if_fail (BIG_IS_BOX (box));

  priv = box->priv;

  if (priv->background_texture)
    {
      guint border_top = 0, border_bottom = 0, border_left = 0, border_right = 0;

      if ((priv->background_x_align == BIG_BOX_ALIGNMENT_FILL) &&
          /* No repeat on the horizontal axis */
         ((priv->background_repeat == BIG_BOX_BACKGROUND_REPEAT_NONE) ||
          (priv->background_repeat == BIG_BOX_BACKGROUND_REPEAT_Y)))
        {
          border_left = priv->background_border_left;
          border_right = priv->background_border_right;
        }

      if ((priv->background_y_align == BIG_BOX_ALIGNMENT_FILL) &&
          /* No repeat on the vertical axis */
         ((priv->background_repeat == BIG_BOX_BACKGROUND_REPEAT_NONE) ||
          (priv->background_repeat == BIG_BOX_BACKGROUND_REPEAT_X)))
        {
          border_top = priv->background_border_top;
          border_bottom = priv->background_border_bottom;
        }

      g_object_set (priv->background_texture,
                    "border-left", border_left,
                    "border-right", border_right,
                    "border-top", border_top,
                    "border-bottom", border_bottom,
                    NULL);
    }
}

/* Update whether or not we draw rounded corners.
 * If different sizes are set for different border segments,
 * we ignore the radius.
 */
static void
big_box_update_draw_rounded_corner (BigBox *box)
{
  BigBoxPrivate *priv;

  priv = box->priv;

  priv->draw_rounded_corner = (priv->border_top == priv->border_left) &&
                              (priv->border_top == priv->border_right) &&
                              (priv->border_top == priv->border_bottom) &&
                              (priv->corner_radius != 0);

  if (!priv->draw_rounded_corner &&
      priv->background_rectangle)
    {
      clutter_actor_unparent(priv->background_rectangle);
      priv->background_rectangle = NULL;
    }

  if (priv->draw_rounded_corner &&
      !priv->background_rectangle)
    {
      priv->background_rectangle = g_object_new (BIG_TYPE_RECTANGLE, NULL);
      clutter_actor_set_parent (priv->background_rectangle, CLUTTER_ACTOR (box));
      clutter_actor_queue_relayout (CLUTTER_ACTOR (box));
    }

  if (priv->draw_rounded_corner)
    {
      g_object_set (priv->background_rectangle,
                    "color", &priv->background_color,
                    "border-color", &priv->border_color,
                    "border-width", CLUTTER_UNITS_TO_DEVICE (priv->border_top),
                    "corner-radius", CLUTTER_UNITS_TO_DEVICE (priv->corner_radius),
                    NULL);
    }
}

static void
big_box_set_background_repeat (BigBox *box,
                               BigBoxBackgroundRepeat background_repeat)
{
  BigBoxPrivate *priv;

  priv = box->priv;

  priv->background_repeat = background_repeat;

  if (priv->background_texture)
    {
      gboolean repeat_x = FALSE, repeat_y = FALSE;

      switch (priv->background_repeat)
        {
        case BIG_BOX_BACKGROUND_REPEAT_NONE:
          break;

        case BIG_BOX_BACKGROUND_REPEAT_X:
          repeat_x = TRUE;
          break;

        case BIG_BOX_BACKGROUND_REPEAT_Y:
          repeat_y = TRUE;
          break;

        case BIG_BOX_BACKGROUND_REPEAT_BOTH:
          repeat_x = TRUE;
          repeat_y = TRUE;
          break;
        }

      g_object_set (G_OBJECT (priv->background_texture),
                    "repeat-x", repeat_x,
                    "repeat-y", repeat_y,
                    NULL);
    }
}

static void
big_box_set_background_pixbuf (BigBox *box, GdkPixbuf *pixbuf)
{
  BigBoxPrivate *priv;
  ClutterActor *background_texture;

  g_return_if_fail (BIG_IS_BOX (box));

  priv = box->priv;

  if (priv->background_texture)
    {
      clutter_actor_unparent (priv->background_texture);
      priv->background_texture = NULL;
    }

  /* This means we just want to remove current background texture  */
  if (pixbuf == NULL)
    return;

  /* The border values are set depending on the background-repeat
   * and background alignment settings in
   * big_box_update_background_border */
  background_texture = big_theme_image_new_from_pixbuf (pixbuf,
                                                        0, 0, 0, 0);

  if (background_texture)
    {
      clutter_actor_set_parent (background_texture, CLUTTER_ACTOR (box));
      priv->background_texture = background_texture;
      big_box_set_background_repeat (box, priv->background_repeat);
    }
}

static void
big_box_set_background_filename (BigBox *box, const char *filename)
{
  BigBoxPrivate *priv;
  ClutterActor *background_texture;

  g_return_if_fail (BIG_IS_BOX (box));
  g_return_if_fail (filename != NULL);

  priv = box->priv;

  if (priv->background_texture)
    {
      clutter_actor_unparent (priv->background_texture);
      priv->background_texture = NULL;
    }

  /* This means we just want to remove current background texture  */
  if (filename == NULL)
    return;

  /* The border values are set depending on the background-repeat
   * and background alignment settings in
   * big_box_update_background_border */
  background_texture = big_theme_image_new_from_file (filename,
                                                      0, 0, 0, 0);

  if (background_texture)
    {
      clutter_actor_set_parent (background_texture, CLUTTER_ACTOR (box));
      priv->background_texture = background_texture;
      big_box_set_background_repeat (box, priv->background_repeat);
    }
}

static void
big_box_set_property (GObject      *gobject,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
  BigBoxPrivate *priv = BIG_BOX (gobject)->priv;
  gboolean need_repaint = TRUE;
  gboolean need_resize = TRUE;

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      priv->orientation = g_value_get_enum (value);
      break;

    case PROP_SPACING:
      priv->spacing = CLUTTER_UNITS_FROM_DEVICE (g_value_get_int (value));
      break;

    case PROP_X_ALIGN:
      priv->x_align = g_value_get_enum (value);
      break;

    case PROP_Y_ALIGN:
      priv->y_align = g_value_get_enum (value);
      break;

    case PROP_PADDING:
      big_box_set_padding(BIG_BOX(gobject),
                          g_value_get_int(value));
      break;

    case PROP_PADDING_TOP:
      priv->padding_top = CLUTTER_UNITS_FROM_DEVICE (g_value_get_int (value));
      break;

    case PROP_PADDING_BOTTOM:
      priv->padding_bottom = CLUTTER_UNITS_FROM_DEVICE (g_value_get_int (value));
      break;

    case PROP_PADDING_LEFT:
      priv->padding_left = CLUTTER_UNITS_FROM_DEVICE(g_value_get_int (value));
      break;

    case PROP_PADDING_RIGHT:
      priv->padding_right = CLUTTER_UNITS_FROM_DEVICE(g_value_get_int (value));
      break;

    case PROP_BORDER:
      big_box_set_border_width(BIG_BOX(gobject),
                               g_value_get_int(value));
      break;

    case PROP_BORDER_TOP:
      priv->border_top = CLUTTER_UNITS_FROM_DEVICE(g_value_get_int (value));
      big_box_update_draw_rounded_corner (BIG_BOX (gobject));
      break;

    case PROP_BORDER_BOTTOM:
      priv->border_bottom = CLUTTER_UNITS_FROM_DEVICE(g_value_get_int (value));
      big_box_update_draw_rounded_corner (BIG_BOX (gobject));
      break;

    case PROP_BORDER_LEFT:
      priv->border_left = CLUTTER_UNITS_FROM_DEVICE(g_value_get_int (value));
      big_box_update_draw_rounded_corner (BIG_BOX (gobject));
      break;

    case PROP_BORDER_RIGHT:
      priv->border_right = CLUTTER_UNITS_FROM_DEVICE(g_value_get_int (value));
      big_box_update_draw_rounded_corner (BIG_BOX (gobject));
      break;

    case PROP_CORNER_RADIUS:
      priv->corner_radius = CLUTTER_UNITS_FROM_DEVICE(g_value_get_uint (value));
      big_box_update_draw_rounded_corner (BIG_BOX (gobject));
      break;

    case PROP_BACKGROUND_BORDER_TOP:
      priv->background_border_top = g_value_get_uint (value);
      need_resize = FALSE;
      big_box_update_background_border (BIG_BOX (gobject));
      break;

    case PROP_BACKGROUND_BORDER_BOTTOM:
      priv->background_border_bottom = g_value_get_uint (value);
      need_resize = FALSE;
      big_box_update_background_border (BIG_BOX (gobject));
      break;

    case PROP_BACKGROUND_BORDER_LEFT:
      priv->background_border_left = g_value_get_uint (value);
      need_resize = FALSE;
      big_box_update_background_border (BIG_BOX (gobject));
      break;

    case PROP_BACKGROUND_BORDER_RIGHT:
      priv->background_border_right = g_value_get_uint (value);
      need_resize = FALSE;
      big_box_update_background_border (BIG_BOX (gobject));
      break;

    case PROP_BACKGROUND_COLOR:
      {
        ClutterColor *color;
        color = g_value_get_boxed (value);
        if (color) {
          priv->background_color = *color;
        } else {
          /* null = default (black and transparent) */
          priv->background_color.red = 0;
          priv->background_color.green = 0;
          priv->background_color.blue = 0;
          priv->background_color.alpha = 0;
        }

        if (priv->background_rectangle)
          {
            g_object_set (priv->background_rectangle,
                          "color", &priv->background_color,
                          NULL);
          }

        need_resize = FALSE;
      }
      break;

    case PROP_BACKGROUND_FILENAME:
      big_box_set_background_filename (BIG_BOX (gobject),
                                       g_value_get_string (value));
      big_box_update_background_border (BIG_BOX (gobject));
      break;

    case PROP_BACKGROUND_PIXBUF:
      big_box_set_background_pixbuf (BIG_BOX (gobject),
                                     g_value_get_object (value));
      big_box_update_background_border (BIG_BOX (gobject));
      break;

    case PROP_BACKGROUND_REPEAT:
      big_box_set_background_repeat (BIG_BOX (gobject),
                                     g_value_get_enum (value));
      big_box_update_background_border (BIG_BOX (gobject));
      break;

    case PROP_BACKGROUND_X_ALIGN:
      priv->background_x_align = g_value_get_enum (value);
      big_box_update_background_border (BIG_BOX (gobject));
      break;

    case PROP_BACKGROUND_Y_ALIGN:
      priv->background_y_align = g_value_get_enum (value);
      big_box_update_background_border (BIG_BOX (gobject));
      break;

    case PROP_BORDER_COLOR:
      {
        ClutterColor *color;
        color = g_value_get_boxed (value);
        if (color) {
          priv->border_color = *color;
        } else {
          /* null = default (black and transparent) */
          priv->border_color.red = 0;
          priv->border_color.green = 0;
          priv->border_color.blue = 0;
          priv->border_color.alpha = 0;
        }

        if (priv->background_rectangle)
          {
            g_object_set (priv->background_rectangle,
                          "border-color", &priv->border_color,
                          NULL);
          }

        need_resize = FALSE;
      }
      break;

    case PROP_DEBUG:
      priv->debug = g_value_get_boolean (value);

      need_repaint = FALSE;
      need_resize = FALSE;
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }

  if (need_resize)
    clutter_actor_queue_relayout (CLUTTER_ACTOR (gobject));
  else if (need_repaint)
    clutter_actor_queue_redraw (CLUTTER_ACTOR (gobject));
}

static void
big_box_get_property (GObject    *gobject,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
  BigBoxPrivate *priv = BIG_BOX (gobject)->priv;

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      g_value_set_enum (value, priv->orientation);
      break;

    case PROP_SPACING:
      g_value_set_int (value, CLUTTER_UNITS_TO_DEVICE(priv->spacing));
      break;

    case PROP_X_ALIGN:
      g_value_set_enum (value, priv->x_align);
      break;

    case PROP_Y_ALIGN:
      g_value_set_enum (value, priv->y_align);
      break;

    case PROP_PADDING_TOP:
      g_value_set_int (value, CLUTTER_UNITS_TO_DEVICE(priv->padding_top));
      break;

    case PROP_PADDING_BOTTOM:
      g_value_set_int (value, CLUTTER_UNITS_TO_DEVICE(priv->padding_bottom));
      break;

    case PROP_PADDING_LEFT:
      g_value_set_int (value, CLUTTER_UNITS_TO_DEVICE(priv->padding_left));
      break;

    case PROP_PADDING_RIGHT:
      g_value_set_int (value, CLUTTER_UNITS_TO_DEVICE(priv->padding_right));
      break;

    case PROP_BORDER_TOP:
      g_value_set_int (value, CLUTTER_UNITS_TO_DEVICE(priv->border_top));
      break;

    case PROP_BORDER_BOTTOM:
      g_value_set_int (value, CLUTTER_UNITS_TO_DEVICE(priv->border_bottom));
      break;

    case PROP_BORDER_LEFT:
      g_value_set_int (value, CLUTTER_UNITS_TO_DEVICE(priv->border_left));
      break;

    case PROP_BORDER_RIGHT:
      g_value_set_int (value,CLUTTER_UNITS_TO_DEVICE(priv->border_right));
      break;

    case PROP_CORNER_RADIUS:
      g_value_set_uint (value,CLUTTER_UNITS_TO_DEVICE (priv->corner_radius));
      break;

    case PROP_BACKGROUND_TEXTURE:
      g_value_set_object (value, priv->background_texture);
      break;

    case PROP_BACKGROUND_RECTANGLE:
      g_value_set_object (value, priv->background_rectangle);
      break;

    case PROP_BACKGROUND_BORDER_TOP:
      g_value_set_uint (value, priv->background_border_top);
      break;

    case PROP_BACKGROUND_BORDER_BOTTOM:
      g_value_set_uint (value, priv->background_border_bottom);
      break;

    case PROP_BACKGROUND_BORDER_LEFT:
      g_value_set_uint (value, priv->background_border_left);
      break;

    case PROP_BACKGROUND_BORDER_RIGHT:
      g_value_set_uint (value, priv->background_border_right);
      break;

    case PROP_BACKGROUND_COLOR:
      g_value_set_boxed (value, &priv->background_color);
      break;

    case PROP_BACKGROUND_REPEAT:
      g_value_set_enum (value, priv->background_repeat);
      break;

    case PROP_BACKGROUND_X_ALIGN:
      g_value_set_enum (value, priv->background_x_align);
      break;

    case PROP_BACKGROUND_Y_ALIGN:
      g_value_set_enum (value, priv->background_y_align);
      break;

    case PROP_BORDER_COLOR:
      g_value_set_boxed (value, &priv->border_color);
      break;

    case PROP_DEBUG:
      g_value_set_boolean (value, priv->debug);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
big_box_finalize (GObject *gobject)
{
  G_OBJECT_CLASS (big_box_parent_class)->finalize (gobject);
}

static void
big_box_dispose (GObject *gobject)
{
  BigBox *self = BIG_BOX (gobject);
  BigBoxPrivate *priv = self->priv;

  if (priv->background_texture)
    {
      clutter_actor_unparent (priv->background_texture);
      priv->background_texture = NULL;
    }

  if (priv->background_rectangle)
    {
      clutter_actor_unparent (priv->background_rectangle);
      priv->background_rectangle = NULL;
    }

  while (priv->children)
    {
      clutter_actor_destroy(((BigBoxChild*)priv->children->data)->actor);
    }

  G_OBJECT_CLASS (big_box_parent_class)->dispose (gobject);
}

static void
big_box_get_content_width_request (ClutterActor  *self,
                                   ClutterUnit   *min_width_p,
                                   ClutterUnit   *natural_width_p)
{
  BigBoxPrivate *priv;
  ClutterUnit total_min;
  ClutterUnit total_natural;
  gint n_children_in_min;
  gint n_children_in_natural;
  GList *c;

  priv = BIG_BOX (self)->priv;

  total_min = 0;
  total_natural = 0;
  n_children_in_min = 0;
  n_children_in_natural = 0;

  for (c = priv->children; c != NULL; c = c->next)
    {
      BigBoxChild *child = (BigBoxChild *) c->data;
      ClutterUnit min_width;
      ClutterUnit natural_width;

      if (!BOX_CHILD_IN_LAYOUT (child))
        continue;

      /* PACK_IF_FITS children are do not contribute to the min size
       * of the whole box, but do contribute to natural size, and
       * will be hidden entirely if their width request does not
       * fit.
       */
      clutter_actor_get_preferred_width (child->actor,
                                         -1,
                                         &min_width,
                                         &natural_width);

      if (priv->debug)
        g_debug ("Child %p min width %d natural %d",
                 child->actor,
                 CLUTTER_UNITS_TO_DEVICE (min_width),
                 CLUTTER_UNITS_TO_DEVICE (natural_width));

      n_children_in_natural += 1;

      /* children with if fits flag won't appear at our min width if
       * we are horizontal. If we're vertical, always request enough
       * width for all if_fits children. Children with 0 min size won't
       * themselves appear but they will get spacing around them, so
       * they count in n_children_in_min.
       */
      if (priv->orientation == BIG_BOX_ORIENTATION_VERTICAL)
        {
          total_min = MAX (total_min, min_width);
          n_children_in_min += 1;

          total_natural = MAX (total_natural, natural_width);
        }
      else
        {
          if (!child->if_fits)
            {
              total_min += min_width;
              n_children_in_min += 1;
            }

          total_natural += natural_width;
        }
    }

  if (priv->orientation == BIG_BOX_ORIENTATION_HORIZONTAL &&
      n_children_in_min > 1)
    {
      total_min += priv->spacing * (n_children_in_min - 1);
    }

  if (priv->orientation == BIG_BOX_ORIENTATION_HORIZONTAL &&
      n_children_in_natural > 1)
    {
      total_natural += priv->spacing * (n_children_in_natural - 1);
    }

  if (min_width_p)
    *min_width_p = total_min;
  if (natural_width_p)
    *natural_width_p = total_natural;
}

static void
big_box_get_preferred_width (ClutterActor  *self,
                             ClutterUnit    for_height,
                             ClutterUnit   *min_width_p,
                             ClutterUnit   *natural_width_p)
{
  BigBoxPrivate *priv;
  ClutterUnit content_min_width;
  ClutterUnit content_natural_width;
  ClutterUnit outside;

  priv = BIG_BOX (self)->priv;

  big_box_get_content_width_request (self,
                                     &content_min_width,
                                     &content_natural_width);

  outside = priv->padding_left + priv->padding_right +
            priv->border_left + priv->border_right;

  if (min_width_p)
    *min_width_p = content_min_width + outside;
  if (natural_width_p)
    *natural_width_p = content_natural_width + outside;

  if (priv->debug)
    {
      if (min_width_p)
        g_debug ("Computed minimum width as %d", CLUTTER_UNITS_TO_DEVICE (*min_width_p));
      if (natural_width_p)
        g_debug ("Computed natural width as %d", CLUTTER_UNITS_TO_DEVICE (*natural_width_p));
    }
}

static void
big_box_get_content_area_horizontal (ClutterActor  *self,
                                     ClutterUnit    requested_content_width,
                                     ClutterUnit    natural_content_width,
                                     ClutterUnit    allocated_box_width,
                                     ClutterUnit   *x_p,
                                     ClutterUnit   *width_p)
{
  BigBoxPrivate *priv;
  ClutterUnit left;
  ClutterUnit right;
  ClutterUnit unpadded_box_width;
  ClutterUnit content_width;

  priv = BIG_BOX (self)->priv;

  left = priv->border_left + priv->padding_left;
  right = priv->border_right + priv->padding_right;

  g_return_if_fail (requested_content_width >= 0);

  if (natural_content_width < allocated_box_width)
    content_width = natural_content_width;
  else
    content_width = MAX (requested_content_width, allocated_box_width);

  unpadded_box_width = allocated_box_width - left - right;

  switch (priv->x_align)
    {
    case BIG_BOX_ALIGNMENT_FIXED:
      g_warning("Must specify a real alignment for content, not FIXED");
      break;
    case BIG_BOX_ALIGNMENT_FILL:
      if (x_p)
        *x_p = left;
      if (width_p)
        *width_p = unpadded_box_width;
      break;
    case BIG_BOX_ALIGNMENT_START:
      if (x_p)
        *x_p = left;
      if (width_p)
        *width_p = content_width;
      break;
    case BIG_BOX_ALIGNMENT_END:
      if (x_p)
        *x_p = allocated_box_width - right - content_width;
      if (width_p)
        *width_p = content_width;
      break;
    case BIG_BOX_ALIGNMENT_CENTER:
      if (x_p)
        *x_p = left + roundf ((unpadded_box_width - content_width) / 2);
      if (width_p)
        *width_p = content_width;
      break;
    }
}

static void
big_box_get_content_area_vertical (ClutterActor   *self,
                                   ClutterUnit     requested_content_height,
                                   ClutterUnit     natural_content_height,
                                   ClutterUnit     allocated_box_height,
                                   ClutterUnit    *y_p,
                                   ClutterUnit    *height_p)
{
  BigBoxPrivate *priv;
  ClutterUnit top;
  ClutterUnit bottom;
  ClutterUnit unpadded_box_height;
  ClutterUnit content_height;

  priv = BIG_BOX (self)->priv;

  top = priv->border_top + priv->padding_top;
  bottom = priv->border_bottom + priv->padding_bottom;

  g_return_if_fail (requested_content_height >= 0);

  if (natural_content_height < allocated_box_height)
    content_height = natural_content_height;
  else
    content_height = MAX (requested_content_height, allocated_box_height);

  unpadded_box_height = allocated_box_height - top - bottom;

  switch (priv->y_align)
    {
    case BIG_BOX_ALIGNMENT_FIXED:
      g_warning("Must specify a real alignment for content, not FIXED");
      break;
    case BIG_BOX_ALIGNMENT_FILL:
      if (y_p)
        *y_p = top;
      if (height_p)
        *height_p = unpadded_box_height;
      break;
    case BIG_BOX_ALIGNMENT_START:
      if (y_p)
        *y_p = top;
      if (height_p)
        *height_p = content_height;
      break;
    case BIG_BOX_ALIGNMENT_END:
      if (y_p)
        *y_p = allocated_box_height - bottom - content_height;
      if (height_p)
        *height_p = content_height;
      break;
    case BIG_BOX_ALIGNMENT_CENTER:
      if (y_p)
        *y_p = top + roundf ((unpadded_box_height - content_height) / 2);
      if (height_p)
        *height_p = content_height;
      break;
    }
}
static BigBoxAdjustInfo *
big_box_adjust_infos_new (BigBox      *box,
                          ClutterUnit  for_content_width)
{
  BigBoxPrivate *priv = box->priv;
  BigBoxAdjustInfo *adjusts = g_new0 (BigBoxAdjustInfo, g_list_length (priv->children));
  GList *c;
  gint i = 0;

  for (c = priv->children; c != NULL; c = c->next)
    {
      BigBoxChild *child = (BigBoxChild *) c->data;

      if (!BOX_CHILD_IN_LAYOUT (child))
        {
          adjusts[i].minimum = adjusts[i].natural = 0;
        }
      else if (priv->orientation == BIG_BOX_ORIENTATION_VERTICAL)
        {
          clutter_actor_get_preferred_height (child->actor, for_content_width,
                                              &adjusts[i].minimum, &adjusts[i].natural);
        }
      else
        {
          clutter_actor_get_preferred_width (child->actor, -1, &adjusts[i].minimum,
                                             &adjusts[i].natural);
        }

      i++;
    }

  return adjusts;
}

static void
big_box_adjust_if_fits_as_not_fitting (GList            *children,
                                       BigBoxAdjustInfo *adjusts)
{
    GList *c;
    gint i;

    i = 0;

    for (c = children; c != NULL; c = c->next)
      {
        BigBoxChild *child = (BigBoxChild *) c->data;

        if (child->if_fits)
          {
            adjusts[i].adjustment -= adjusts[i].minimum;
            adjusts[i].does_not_fit = TRUE;
          }

        ++i;
      }
}

static gboolean
big_box_adjust_up_to_natural_size (GList            *children,
                                   ClutterUnit      *remaining_extra_space_p,
                                   BigBoxAdjustInfo *adjusts,
                                   gboolean          if_fits)
{
  ClutterUnit smallest_increase;
  ClutterUnit space_to_distribute;
  GList *c;
  gint n_needing_increase;
  gint i;

  g_assert (*remaining_extra_space_p >= 0);

  if (*remaining_extra_space_p == 0)
    return FALSE;

  smallest_increase = CLUTTER_MAXUNIT;
  n_needing_increase = 0;

  i = 0;

  for (c = children; c != NULL; c = c->next)
    {
      BigBoxChild *child = (BigBoxChild *) c->data;

      if (BOX_CHILD_IN_LAYOUT (child) &&
          ((!child->if_fits && !if_fits) ||
           (child->if_fits && if_fits && !adjusts[i].does_not_fit)))
        {
          ClutterUnit needed_increase;

          g_assert (adjusts[i].adjustment >= 0);

          /* Guaranteed to be >= 0 */
          needed_increase = adjusts[i].natural - adjusts[i].minimum;

          g_assert (needed_increase >= 0);

          needed_increase -= adjusts[i].adjustment; /* see how much we've already increased */

          if (needed_increase > 0)
            {
              n_needing_increase += 1;
              smallest_increase = MIN (smallest_increase, needed_increase);
            }
        }

      ++i;
    }

  if (n_needing_increase == 0)
    return FALSE;

  g_assert (smallest_increase < G_MAXINT);

  space_to_distribute = MIN (*remaining_extra_space_p,
                             smallest_increase * n_needing_increase);

  g_assert(space_to_distribute >= 0);
  g_assert(space_to_distribute <= *remaining_extra_space_p);

  *remaining_extra_space_p -= space_to_distribute;

  i = 0;

  for (c = children; c != NULL; c = c->next)
    {
      BigBoxChild *child = (BigBoxChild *) c->data;

      if (BOX_CHILD_IN_LAYOUT (child) &&
          ((!child->if_fits && !if_fits) ||
           (child->if_fits && if_fits && !adjusts[i].does_not_fit)))
        {
          ClutterUnit needed_increase;

          g_assert (adjusts[i].adjustment >= 0);

          /* Guaranteed to be >= 0 */
          needed_increase = adjusts[i].natural - adjusts[i].minimum;

          g_assert(needed_increase >= 0);

          needed_increase -= adjusts[i].adjustment; /* see how much we've already increased */

          if (needed_increase > 0)
            {
              ClutterUnit extra;

              extra = (space_to_distribute / n_needing_increase);

              n_needing_increase -= 1;
              space_to_distribute -= extra;
              adjusts[i].adjustment += extra;
            }
        }

      ++i;
    }

  g_assert (n_needing_increase == 0);
  g_assert (space_to_distribute == 0);

  return TRUE;
}

static gboolean
big_box_adjust_one_if_fits (GList             *children,
                            ClutterUnit        spacing,
                            ClutterUnit       *remaining_extra_space_p,
                            BigBoxAdjustInfo  *adjusts)
{
  GList *c;
  ClutterUnit spacing_delta;
  gint i;
  gboolean visible_children = FALSE;

  if (*remaining_extra_space_p == 0)
    return FALSE;

  /* if there are no currently visible children, then adding a child won't
   * add another spacing
   */
  i = 0;

  for (c = children; c != NULL; c = c->next) {
      BigBoxChild *child = (BigBoxChild *) c->data;

      if (BOX_CHILD_IN_LAYOUT (child) &&
          (!child->if_fits || !adjusts[i].does_not_fit))
        {
          visible_children = TRUE;
          break;
        }

      i++;
  }

  spacing_delta = visible_children ? spacing : 0;

  i = 0;

  for (c = children; c != NULL; c = c->next)
    {
      if (adjusts[i].does_not_fit)
        {
          /* This child was adjusted downward, see if we can pop it visible
           * (picking the smallest instead of first if-fits child on each pass
           * might be nice, but for now it's the first that fits)
           */
          if ((adjusts[i].minimum + spacing_delta) <= *remaining_extra_space_p)
            {
              adjusts[i].adjustment += adjusts[i].minimum;

              g_assert (adjusts[i].adjustment >= 0);

              adjusts[i].does_not_fit = FALSE;
              *remaining_extra_space_p -= (adjusts[i].minimum + spacing_delta);

              g_assert (*remaining_extra_space_p >= 0);

              return TRUE;
            }
        }

      ++i;
    }

  return FALSE;
}

static gboolean
box_child_is_expandable (BigBoxChild *child, BigBoxAdjustInfo *adjust)
{
  return (BOX_CHILD_IS_VISIBLE (child) || child->if_hidden) && child->expand &&
         (!child->if_fits || (adjust && !(adjust->does_not_fit)));
}

static int
big_box_count_expandable_children (GList           *children,
                                   BigBoxAdjustInfo *adjusts)
{
  GList *c;
  gint count;
  gint i;

  count = 0;
  i = 0;

  for (c = children; c != NULL; c = c->next)
    {
      BigBoxChild *child = (BigBoxChild *) c->data;

      /* We assume here that we've prevented via g_warning
       * any floats/fixed from having expand=TRUE
       */
      if (box_child_is_expandable (child, adjusts ? &(adjusts[i]) : NULL))
        ++count;

      ++i;
    }

  return count;
}

static void
big_box_adjust_for_expandable (GList            *children,
                               ClutterUnit      *remaining_extra_space_p,
                               BigBoxAdjustInfo *adjusts)
{
  GList *c;
  ClutterUnit expand_space;
  gint expand_count;
  gint i;

  if (*remaining_extra_space_p == 0)
    return;

  expand_space = *remaining_extra_space_p;
  expand_count = big_box_count_expandable_children (children, adjusts);

  if (expand_count == 0)
    return;

  i = 0;

  for (c = children; c != NULL; c = c->next)
    {
      BigBoxChild *child = (BigBoxChild *) c->data;

      if (box_child_is_expandable (child, &(adjusts[i])) &&
          !adjusts[i].does_not_fit)
        {
          ClutterUnit extra;

          extra = (expand_space / expand_count);

          expand_count -= 1;
          expand_space -= extra;
          adjusts[i].adjustment += extra;
        }

      ++i;
    }

  /* if we had anything to expand, then we will have used up all space */
  g_assert (expand_space == 0);
  g_assert (expand_count == 0);

  *remaining_extra_space_p = 0;
}
static void
big_box_compute_adjusts (GList             *children,
                         BigBoxAdjustInfo  *adjusts,
                         ClutterUnit        spacing,
                         ClutterUnit        alloc_request_delta)
{
  ClutterUnit remaining_extra_space;

  if (children == NULL)
    return;

  /* Go ahead and cram all PACK_IF_FITS children to zero width,
   * we'll expand them again if we can.
   */
  big_box_adjust_if_fits_as_not_fitting (children, adjusts);

  /* Make no adjustments if we got too little or just right space.
   * (FIXME handle too little space better)
   */
  if (alloc_request_delta <= 0) {
      return;
  }

  remaining_extra_space = alloc_request_delta;

  /* Adjust non-PACK_IF_FITS up to natural size */
  while (big_box_adjust_up_to_natural_size (children,
                                            &remaining_extra_space, adjusts,
                                            FALSE))
      ;

  /* See if any PACK_IF_FITS can get their minimum size */
  while (big_box_adjust_one_if_fits (children,
                                     spacing, &remaining_extra_space, adjusts))
      ;

  /* If so, then see if they can also get a natural size */
  while (big_box_adjust_up_to_natural_size(children,
                                           &remaining_extra_space, adjusts,
                                           TRUE))
      ;

  /* And finally we can expand to fill empty space */
  big_box_adjust_for_expandable (children, &remaining_extra_space, adjusts);

  /* remaining_extra_space need not be 0, if we had no expandable children */
}

static ClutterUnit
big_box_get_adjusted_size (BigBoxAdjustInfo  *adjust)
{
  return adjust->minimum + adjust->adjustment;
}

static void
big_box_get_hbox_height_request (ClutterActor  *self,
                                 ClutterUnit    for_width,
                                 ClutterUnit   *min_height_p,
                                 ClutterUnit   *natural_height_p)
{
  BigBoxPrivate *priv;
  ClutterUnit total_min;
  ClutterUnit total_natural;
  ClutterUnit requested_content_width;
  ClutterUnit natural_content_width;
  ClutterUnit allocated_content_width;
  BigBoxAdjustInfo *width_adjusts;
  GList *c;
  gint i;

  priv = BIG_BOX (self)->priv;

  total_min = 0;
  total_natural = 0;

  big_box_get_content_width_request (self, &requested_content_width,
                                     &natural_content_width);

  big_box_get_content_area_horizontal (self,
                                       requested_content_width,
                                       natural_content_width,
                                       for_width, NULL,
                                       &allocated_content_width);

  width_adjusts = big_box_adjust_infos_new (BIG_BOX (self), for_width);

  big_box_compute_adjusts (priv->children,
                           width_adjusts,
                           priv->spacing,
                           allocated_content_width - requested_content_width);

  i = 0;

  for (c = priv->children; c != NULL; c = c->next)
    {
      BigBoxChild *child = c->data;
      ClutterUnit min_height, natural_height;
      ClutterUnit req = 0;

      if (!BOX_CHILD_IN_LAYOUT (child))
        {
          ++i;
          continue;
        }

      req = big_box_get_adjusted_size (&width_adjusts[i]);

      clutter_actor_get_preferred_height (child->actor, req,
                                          &min_height, &natural_height);

      if (priv->debug)
        g_debug ("H - Child %p min height %d natural %d",
                 child->actor,
                 CLUTTER_UNITS_TO_DEVICE (min_height),
                 CLUTTER_UNITS_TO_DEVICE (natural_height));

      total_min = MAX (total_min, min_height);
      total_natural = MAX (total_natural, natural_height);

      ++i;
    }

  g_free (width_adjusts);

  if (min_height_p)
    *min_height_p = total_min;
  if (natural_height_p)
    *natural_height_p = total_natural;
}

static void
big_box_get_vbox_height_request (ClutterActor  *self,
                                 ClutterUnit    for_width,
                                 ClutterUnit   *min_height_p,
                                 ClutterUnit   *natural_height_p)
{
  BigBoxPrivate *priv;
  ClutterUnit total_min;
  ClutterUnit total_natural;
  gint n_children_in_min;
  gint n_children_in_natural;
  GList *c;

  priv = BIG_BOX (self)->priv;

  total_min = 0;
  total_natural = 0;
  n_children_in_min = 0;
  n_children_in_natural = 0;

  for (c = priv->children; c != NULL; c = c->next)
    {
      BigBoxChild *child = (BigBoxChild *) c->data;
      ClutterUnit min_height;
      ClutterUnit natural_height;

      if (!BOX_CHILD_IN_LAYOUT (child))
        continue;

      clutter_actor_get_preferred_height (child->actor, for_width,
                                          &min_height, &natural_height);

      if (priv->debug)
        g_debug ("V - Child %p min height %d natural %d",
                 child->actor,
                 CLUTTER_UNITS_TO_DEVICE (min_height),
                 CLUTTER_UNITS_TO_DEVICE (natural_height));

      n_children_in_natural += 1;
      total_natural += natural_height;

      if (!child->if_fits)
        {
          n_children_in_min += 1;
          total_min += min_height;
        }
    }

  if (n_children_in_min > 1)
    total_min += priv->spacing * (n_children_in_min - 1);
  if (n_children_in_natural > 1)
    total_natural += priv->spacing * (n_children_in_natural - 1);

  if (min_height_p)
    *min_height_p = total_min;
  if (natural_height_p)
    *natural_height_p = total_natural;
}

static void
big_box_get_content_height_request (ClutterActor  *self,
                                    ClutterUnit    for_width,
                                    ClutterUnit   *min_height_p,
                                    ClutterUnit   *natural_height_p)
{
  BigBoxPrivate *priv;

  priv = BIG_BOX (self)->priv;

  if (priv->orientation == BIG_BOX_ORIENTATION_VERTICAL)
    big_box_get_vbox_height_request (self, for_width,
                                     min_height_p, natural_height_p);
  else
    big_box_get_hbox_height_request (self, for_width,
                                     min_height_p, natural_height_p);
}

static void
big_box_get_preferred_height (ClutterActor  *self,
                              ClutterUnit    for_width,
                              ClutterUnit   *min_height_p,
                              ClutterUnit   *natural_height_p)
{
  BigBoxPrivate *priv;
  ClutterUnit content_min_height, content_natural_height;
  ClutterUnit content_for_width;
  ClutterUnit outside;

  priv = BIG_BOX (self)->priv;

  content_for_width = for_width
      - priv->padding_left - priv->padding_right
      - priv->border_left - priv->border_right;

  /* We need to call this even if just returning the box-height prop,
   * so that children can rely on getting the full request, allocate
   * cycle in order every time, and so we compute the cached requests.
   */
  big_box_get_content_height_request (self,
                                      content_for_width,
                                      &content_min_height,
                                      &content_natural_height);

  outside = priv->padding_top + priv->padding_bottom +
            priv->border_top + priv->border_bottom;

  if (min_height_p)
      *min_height_p = content_min_height + outside;
  if (natural_height_p)
      *natural_height_p = content_natural_height + outside;


  if (priv->debug)
    {
      if (min_height_p)
        g_debug ("Computed minimum height for width=%d as %d",
                 CLUTTER_UNITS_TO_DEVICE (for_width), CLUTTER_UNITS_TO_DEVICE (*min_height_p));
      if (natural_height_p)
        g_debug ("Computed natural height for width=%d as %d",
                 CLUTTER_UNITS_TO_DEVICE (for_width), CLUTTER_UNITS_TO_DEVICE (*natural_height_p));
    }
}

static void
big_box_layout (ClutterActor    *self,
                ClutterUnit      content_x,
                ClutterUnit      content_y,
                ClutterUnit      allocated_content_width,
                ClutterUnit      allocated_content_height,
                ClutterUnit      requested_content_width,
                ClutterUnit      requested_content_height,
                gboolean         absolute_origin_changed)
{
  BigBoxPrivate *priv;
  BigBoxAdjustInfo *adjusts;
  ClutterActorBox child_box;
  ClutterUnit allocated_size, requested_size;
  ClutterUnit start;
  ClutterUnit end;
  GList *c;
  gint i;

  priv = BIG_BOX (self)->priv;

  if (priv->orientation == BIG_BOX_ORIENTATION_VERTICAL)
    {
      allocated_size = allocated_content_height;
      requested_size = requested_content_height;
      start = content_y;
    }
  else
    {
      allocated_size = allocated_content_width;
      requested_size = requested_content_width;
      start = content_x;
    }

  end = start + allocated_size;

  adjusts = big_box_adjust_infos_new (BIG_BOX (self), allocated_content_width);

  big_box_compute_adjusts (priv->children,
                           adjusts,
                           priv->spacing,
                           allocated_size - requested_size);

  i = 0;

  for (c = priv->children; c != NULL; c = c->next)
   {
      BigBoxChild *child = (BigBoxChild *) c->data;
      ClutterUnit req;

      if (!BOX_CHILD_IN_LAYOUT (child))
        {
          ++i;
          continue;
        }

      if (priv->orientation == BIG_BOX_ORIENTATION_VERTICAL)
        {
          req = big_box_get_adjusted_size (&adjusts[i]);

          child_box.x1 = content_x;
          child_box.y1 = child->end ? end - req : start;
          child_box.x2 = child_box.x1 + allocated_content_width;
          child_box.y2 = child_box.y1 + req;

          if (priv->debug)
            g_debug ("V - Child %p %s being allocated: %d, %d, %d, %d w: %d h: %d",
                     child->actor,
                     g_type_name_from_instance((GTypeInstance*) child->actor),
                     CLUTTER_UNITS_TO_DEVICE (child_box.x1),
                     CLUTTER_UNITS_TO_DEVICE (child_box.y1),
                     CLUTTER_UNITS_TO_DEVICE (child_box.x2),
                     CLUTTER_UNITS_TO_DEVICE (child_box.y2),
                     CLUTTER_UNITS_TO_DEVICE (child_box.x2 - child_box.x1),
                     CLUTTER_UNITS_TO_DEVICE (child_box.y2 - child_box.y1));

          clutter_actor_allocate (child->actor, &child_box,
                                  absolute_origin_changed);
        }
      else
        {
          req = big_box_get_adjusted_size (&adjusts[i]);

          child_box.x1 = child->end ? end - req : start;
          child_box.y1 = content_y;
          child_box.x2 = child_box.x1 + req;
          child_box.y2 = child_box.y1 + allocated_content_height;

          if (priv->debug)
            g_debug ("H - Child %p %s being allocated: %d, %d, %d, %d  w: %d h: %d",
                     child->actor,
                     g_type_name_from_instance((GTypeInstance*) child->actor),
                     CLUTTER_UNITS_TO_DEVICE (child_box.x1),
                     CLUTTER_UNITS_TO_DEVICE (child_box.y1),
                     CLUTTER_UNITS_TO_DEVICE (child_box.x2),
                     CLUTTER_UNITS_TO_DEVICE (child_box.y2),
                     CLUTTER_UNITS_TO_DEVICE (child_box.x2 - child_box.x1),
                     CLUTTER_UNITS_TO_DEVICE (child_box.y2 - child_box.y1));

          clutter_actor_allocate (child->actor, &child_box,
                                  absolute_origin_changed);
        }

      if (req <= 0)
        {
          /* Child was adjusted out of existence, act like it's
           * !visible
           */
          child_box.x1 = 0;
          child_box.y1 = 0;
          child_box.x2 = 0;
          child_box.y2 = 0;

          clutter_actor_allocate (child->actor, &child_box,
                                  absolute_origin_changed);
        }

      /* Children with req == 0 still get spacing unless they are IF_FITS.
       * The handling of spacing could use improvement (spaces should probably
       * act like items with min width 0 and natural width of spacing) but
       * it's pretty hard to get right without rearranging the code a lot.
       */
      if (!adjusts[i].does_not_fit)
        {
          if (child->end)
            end -= (req + priv->spacing);
          else
            start += (req + priv->spacing);
        }

      ++i;
    }

  g_free (adjusts);
}

static void
big_box_allocate (ClutterActor          *self,
                  const ClutterActorBox *box,
                  gboolean               absolute_origin_changed)
{
  BigBoxPrivate *priv;
  ClutterUnit requested_content_width;
  ClutterUnit requested_content_height;
  ClutterUnit natural_content_width;
  ClutterUnit natural_content_height;
  ClutterUnit allocated_content_width;
  ClutterUnit allocated_content_height = 0;
  ClutterUnit content_x, content_y = 0;
  GList *c;

  priv = BIG_BOX (self)->priv;

  if (priv->debug)
    g_debug ("Entire box %p being allocated: %d, %d, %d, %d",
             self,
             CLUTTER_UNITS_TO_DEVICE (box->x1),
             CLUTTER_UNITS_TO_DEVICE (box->y1),
             CLUTTER_UNITS_TO_DEVICE (box->x2),
             CLUTTER_UNITS_TO_DEVICE (box->y2));

  CLUTTER_ACTOR_CLASS (big_box_parent_class)->allocate (self, box, absolute_origin_changed);

  big_box_get_content_width_request (self,
                                     &requested_content_width,
                                     &natural_content_width);

  big_box_get_content_area_horizontal (self, requested_content_width,
                                       natural_content_width, box->x2 - box->x1,
                                       &content_x, &allocated_content_width);

  big_box_get_content_height_request (self,
                                      allocated_content_width,
                                      &requested_content_height,
                                      &natural_content_height);

  big_box_get_content_area_vertical (self, requested_content_height,
                                     natural_content_height, box->y2 - box->y1,
                                     &content_y, &allocated_content_height);

  if (priv->debug)
    {
      if (allocated_content_height < requested_content_height)
        g_debug ("Box %p allocated height %d but requested %d",
                 self,
                 CLUTTER_UNITS_TO_DEVICE (allocated_content_height),
                 CLUTTER_UNITS_TO_DEVICE (requested_content_height));
      if (allocated_content_width < requested_content_width)
        g_debug ("Box %p allocated width %d but requested %d",
                 self,
                 CLUTTER_UNITS_TO_DEVICE (allocated_content_width),
                 CLUTTER_UNITS_TO_DEVICE (requested_content_width));
    }

  if (priv->background_texture)
    {
      ClutterActorBox bg_box;

      big_box_get_bg_texture_allocation (self,
                                         box->x2 - box->x1,
                                         box->y2 - box->y1,
                                         &bg_box);

      if (priv->debug)
        {
          g_debug ("Box %p texture allocated width %d and height %d",
                   self,
                   CLUTTER_UNITS_TO_DEVICE (bg_box.x2 - bg_box.x1),
                   CLUTTER_UNITS_TO_DEVICE (bg_box.y2 - bg_box.y1));
        }

      clutter_actor_allocate (priv->background_texture, &bg_box,
                              absolute_origin_changed);
    }

  if (priv->background_rectangle)
    {
      ClutterActorBox rectangle_box;

      rectangle_box.x1 = 0;
      rectangle_box.y1 = 0;
      rectangle_box.x2 = box->x2 - box->x1;
      rectangle_box.y2 = box->y2 - box->y1;

      clutter_actor_allocate (priv->background_rectangle,
                              &rectangle_box,
                              absolute_origin_changed);
    }

  for (c = priv->children; c != NULL; c = c->next)
    {
      BigBoxChild *child = (BigBoxChild *) c->data;
      ClutterActorBox child_box;

      if (!(BOX_CHILD_IS_VISIBLE (child) || child->if_hidden))
        {
          child_box.x1 = 0;
          child_box.y1 = 0;
          child_box.x2 = 0;
          child_box.y2 = 0;

          clutter_actor_allocate (child->actor, &child_box, FALSE);
        }
      else if (child->fixed)
        {
          ClutterUnit x, y, width, height;

          clutter_actor_get_positionu (child->actor, &x, &y);
          clutter_actor_get_preferred_width(child->actor, -1, NULL, &width);
          clutter_actor_get_preferred_height(child->actor, width, NULL, &height);

          switch (child->fixed_x_align)
            {
            case BIG_BOX_ALIGNMENT_FIXED:
              /* honor child forced x,y instead of aligning automatically */
              child_box.x1 = x;
              child_box.x2 = x + width;
              break;
            case BIG_BOX_ALIGNMENT_START:
              child_box.x1 = content_x;
              child_box.x2 = child_box.x1 + width;
              break;
            case BIG_BOX_ALIGNMENT_END:
              child_box.x2 = content_x + allocated_content_width;
              child_box.x1 = child_box.x2 - width;
              break;
            case BIG_BOX_ALIGNMENT_CENTER:
              child_box.x1 = content_x + roundf ((allocated_content_width - width) / 2);
              child_box.x2 = child_box.x1 + width;
              break;
            case BIG_BOX_ALIGNMENT_FILL:
              child_box.x1 = content_x;
              child_box.x2 = content_x + allocated_content_width;
              break;
            }

          switch (child->fixed_y_align)
            {
            case BIG_BOX_ALIGNMENT_FIXED:
              /* honor child forced x,y instead of aligning automatically */
              child_box.y1 = y;
              child_box.y2 = y + height;
              break;
            case BIG_BOX_ALIGNMENT_START:
              child_box.y1 = content_y;
              child_box.y2 = child_box.y1 + height;
              break;
            case BIG_BOX_ALIGNMENT_END:
              child_box.y2 = content_y + allocated_content_height;
              child_box.y1 = child_box.y2 - height;
              break;
            case BIG_BOX_ALIGNMENT_CENTER:
              child_box.y1 = content_y + roundf ((allocated_content_height - height) / 2);
              child_box.y2 = child_box.y1 + height;
              break;
            case BIG_BOX_ALIGNMENT_FILL:
              child_box.y1 = content_y;
              child_box.y2 = content_y + allocated_content_height;
              break;
            }

          if (priv->debug)
            g_debug ("Fixed Child being allocated: %d, %d, %d, %d",
                     CLUTTER_UNITS_TO_DEVICE (child_box.x1),
                     CLUTTER_UNITS_TO_DEVICE (child_box.y1),
                     CLUTTER_UNITS_TO_DEVICE (child_box.x2),
                     CLUTTER_UNITS_TO_DEVICE (child_box.y2));

          clutter_actor_allocate(child->actor, &child_box,
                                 absolute_origin_changed);
        }
    }

  big_box_layout (self, content_x, content_y,
                  allocated_content_width, allocated_content_height,
                  requested_content_width, requested_content_height,
                  absolute_origin_changed);
}

static void
big_box_paint (ClutterActor *actor)
{
  BigBox *self = BIG_BOX (actor);
  GList *c;
  ClutterColor color;
  guint8 actor_opacity;
  ClutterGeometry allocation;
  int border_top, border_bottom, border_left, border_right;
  int padding_top, padding_bottom, padding_left, padding_right;

  /* First, convert everything to pixels */
  border_top = CLUTTER_UNITS_TO_DEVICE (self->priv->border_top);
  border_bottom = CLUTTER_UNITS_TO_DEVICE (self->priv->border_bottom);
  border_left = CLUTTER_UNITS_TO_DEVICE (self->priv->border_left);
  border_right = CLUTTER_UNITS_TO_DEVICE (self->priv->border_right);
  padding_top = CLUTTER_UNITS_TO_DEVICE (self->priv->padding_top);
  padding_bottom = CLUTTER_UNITS_TO_DEVICE (self->priv->padding_bottom);
  padding_left = CLUTTER_UNITS_TO_DEVICE (self->priv->padding_left);
  padding_right = CLUTTER_UNITS_TO_DEVICE (self->priv->padding_right);

  actor_opacity = clutter_actor_get_paint_opacity (actor);

  /* Remember allocation.x and .y don't matter since we are
   * theoretically already translated to 0,0
   */
  clutter_actor_get_allocation_geometry (actor, &allocation);

  cogl_push_matrix ();

  /* Background */

  color = self->priv->background_color;
  color.alpha = (color.alpha * actor_opacity) / 0xff;

  /* Border */

  if (self->priv->draw_rounded_corner)
    {
      /* we delegate drawing of the background to BigRectangle.
       * Ideally we would always do that but currently BigRectangle
       * does not support border segments with different size
       */
      clutter_actor_paint (self->priv->background_rectangle);
    }
  else
    {
      if (color.alpha != 0)
        {
          cogl_set_source_color4ub (color.red, color.green, color.blue, color.alpha);

          cogl_rectangle (border_left, border_top,
                          allocation.width - border_left,
                          allocation.height - border_bottom);
        }

      color = self->priv->border_color;
      color.alpha = (color.alpha * actor_opacity) / 0xff;

      if (color.alpha != 0)
        {
          cogl_set_source_color4ub (color.red, color.green, color.blue, color.alpha);

          /* top */
          cogl_rectangle (0, 0,
                          allocation.width,
                          border_top);

          /* left */
          cogl_rectangle (0, border_top,
                          border_left,
                          allocation.height - border_top);

          /* right */
          cogl_rectangle (allocation.width - border_right,
                          border_top,
                          allocation.width,
                          allocation.height - border_top);

          /* bottom */
          cogl_rectangle (0, allocation.height - border_bottom,
                          allocation.width,
                          allocation.height);
        }
    }

  if (self->priv->background_texture &&
      CLUTTER_ACTOR_IS_VISIBLE(self->priv->background_texture)) {
    clutter_actor_paint (CLUTTER_ACTOR (self->priv->background_texture));
  }

  /* Children */

  for (c = self->priv->children;
       c != NULL;
       c = c->next)
    {
      BigBoxChild *child = (BigBoxChild *) c->data;

      g_assert (child != NULL);

      if (BOX_CHILD_IS_VISIBLE (child)) {
        clutter_actor_paint (child->actor);
      }
    }

  cogl_pop_matrix();
}

static void
big_box_pick (ClutterActor       *actor,
              const ClutterColor *color)
{
  BigBox *self = BIG_BOX (actor);
  GList *c;

  CLUTTER_ACTOR_CLASS (big_box_parent_class)->pick (actor, color);

  for (c = self->priv->children;
       c != NULL;
       c = c->next)
    {
      BigBoxChild *child = (BigBoxChild *) c->data;

      g_assert (child != NULL);

      if (BOX_CHILD_IS_VISIBLE (child))
        clutter_actor_paint (child->actor);
    }
}

static void
big_box_class_init (BigBoxClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  g_type_class_add_private (klass, sizeof (BigBoxPrivate));

  gobject_class->set_property = big_box_set_property;
  gobject_class->get_property = big_box_get_property;
  gobject_class->dispose = big_box_dispose;
  gobject_class->finalize = big_box_finalize;

  actor_class->get_preferred_width = big_box_get_preferred_width;
  actor_class->get_preferred_height = big_box_get_preferred_height;
  actor_class->allocate = big_box_allocate;
  actor_class->paint = big_box_paint;
  actor_class->pick  = big_box_pick;

  g_object_class_install_property
                 (gobject_class,
                  PROP_ORIENTATION,
                  g_param_spec_enum ("orientation",
                                     "Orientation",
                                     "Orientation of the box",
                                     BIG_TYPE_BOX_ORIENTATION,
                                     BIG_BOX_ORIENTATION_VERTICAL,
                                     G_PARAM_READWRITE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_SPACING,
                  g_param_spec_int ("spacing",
                                    "Spacing",
                                    "Spacing between items of the box",
                                    G_MININT,
                                    G_MAXINT,
                                    0,
                                    G_PARAM_READWRITE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_X_ALIGN,
                  g_param_spec_enum ("x-align",
                                     "X alignment",
                                     "X alignment",
                                     BIG_TYPE_BOX_ALIGNMENT,
                                     BIG_BOX_ALIGNMENT_START,
                                     G_PARAM_READWRITE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_Y_ALIGN,
                  g_param_spec_enum ("y-align",
                                     "Y alignment",
                                     "Y alignment",
                                     BIG_TYPE_BOX_ALIGNMENT,
                                     BIG_BOX_ALIGNMENT_START,
                                    G_PARAM_READWRITE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_PADDING,
                  g_param_spec_int ("padding",
                                    "Padding",
                                    "Padding (set all paddings at once)",
                                    G_MININT,
                                    G_MAXINT,
                                    0,
                                    G_PARAM_WRITABLE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_PADDING_TOP,
                  g_param_spec_int ("padding-top",
                                    "Padding on the top",
                                    "Padding on the top",
                                    G_MININT,
                                    G_MAXINT,
                                    0,
                                    G_PARAM_READWRITE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_PADDING_BOTTOM,
                  g_param_spec_int ("padding-bottom",
                                    "Padding on the bottom",
                                    "Padding on the bottom",
                                    G_MININT,
                                    G_MAXINT,
                                    0,
                                    G_PARAM_READWRITE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_PADDING_LEFT,
                  g_param_spec_int ("padding-left",
                                    "Padding on the left",
                                    "Padding on the left",
                                    G_MININT,
                                    G_MAXINT,
                                    0,
                                    G_PARAM_READWRITE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_PADDING_RIGHT,
                  g_param_spec_int ("padding-right",
                                    "Padding on the right",
                                    "Padding on the right",
                                    G_MININT,
                                    G_MAXINT,
                                    0,
                                    G_PARAM_READWRITE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_BORDER,
                  g_param_spec_int ("border",
                                    "Border",
                                    "Border (set all borders at once)",
                                    G_MININT,
                                    G_MAXINT,
                                    0,
                                    G_PARAM_WRITABLE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_BORDER_TOP,
                  g_param_spec_int ("border-top",
                                    "Border on the top",
                                    "Border on the top",
                                    G_MININT,
                                    G_MAXINT,
                                    0,
                                    G_PARAM_READWRITE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_BORDER_BOTTOM,
                  g_param_spec_int ("border-bottom",
                                    "Border on the bottom",
                                    "Border on the bottom",
                                    G_MININT,
                                    G_MAXINT,
                                    0,
                                    G_PARAM_READWRITE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_BORDER_LEFT,
                  g_param_spec_int ("border-left",
                                    "Border on the left",
                                    "Border on the left",
                                    G_MININT,
                                    G_MAXINT,
                                    0,
                                    G_PARAM_READWRITE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_BORDER_RIGHT,
                  g_param_spec_int ("border-right",
                                    "Border on the right",
                                    "Border on the right",
                                    G_MININT,
                                    G_MAXINT,
                                    0,
                                    G_PARAM_READWRITE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_CORNER_RADIUS,
                  g_param_spec_uint ("corner-radius",
                                     "Corner radius",
                                     "Radius of the rounded corner "
                                     "(ignored with border segments of "
                                     "different sizes)",
                                     0,
                                     G_MAXUINT,
                                     0,
                                     G_PARAM_READWRITE));

  /**
   * BigBox:background-border-top:
   *
   * Specifies a border on the top of the image which should not be
   * stretched when the image is set to fill the box vertically
   * (background-y-align set to FILL, background-repeat set to
   * not repeat vertically. Useful for images with rounded corners.
   */
  g_object_class_install_property
                 (gobject_class,
                  PROP_BACKGROUND_BORDER_TOP,
                  g_param_spec_uint ("background-border-top",
                                     "Background border on the top",
                                     "Background border on the top",
                                     0,
                                     G_MAXUINT,
                                     0,
                                     G_PARAM_READWRITE));

  /**
   * BigBox:background-border-bottom:
   *
   * Specifies a border on the bottom of the image which should not be
   * stretched when the image is set to fill the box vertically
   * (background-y-align set to FILL, background-repeat set to
   * not repeat vertically. Useful for images with rounded corners.
   */
  g_object_class_install_property
                 (gobject_class,
                  PROP_BACKGROUND_BORDER_BOTTOM,
                  g_param_spec_uint ("background-border-bottom",
                                     "Background border on the bottom",
                                     "Background border on the bottom",
                                     0,
                                     G_MAXUINT,
                                     0,
                                     G_PARAM_READWRITE));

  /**
   * BigBox:background-border-left:
   *
   * Specifies a border on the left of the image which should not be
   * stretched when the image is set to fill the box horizontally
   * (background-x-align set to FILL, background-repeat set to
   * not repeat horizontally. Useful for images with rounded corners.
   */
  g_object_class_install_property
                 (gobject_class,
                  PROP_BACKGROUND_BORDER_LEFT,
                  g_param_spec_uint ("background-border-left",
                                     "Background border on the left",
                                     "Background border on the left",
                                     0,
                                     G_MAXUINT,
                                     0,
                                     G_PARAM_READWRITE));

  /**
   * BigBox:background-border-right:
   *
   * Specifies a border on the right of the image which should not be
   * stretched when the image is set to fill the box horizontally
   * (background-x-align set to FILL, background-repeat set to
   * not repeat horizontally. Useful for images with rounded corners.
   */
  g_object_class_install_property
                 (gobject_class,
                  PROP_BACKGROUND_BORDER_RIGHT,
                  g_param_spec_uint ("background-border-right",
                                     "Background border on the right",
                                     "Background border on the right",
                                     0,
                                     G_MAXUINT,
                                     0,
                                     G_PARAM_READWRITE));

  /**
   * BigBox:background-color:
   *
   * Background color, covers padding but not border.
   */
  g_object_class_install_property
    (gobject_class,
     PROP_BACKGROUND_COLOR,
     g_param_spec_boxed ("background-color",
                         "Background Color",
                         "The color of the background",
                         CLUTTER_TYPE_COLOR,
                         G_PARAM_READWRITE));
  /**
   * BigBox:background-filename:
   *
   * Background filename, covers padding but not border.
   */
  g_object_class_install_property
    (gobject_class,
     PROP_BACKGROUND_FILENAME,
     g_param_spec_string ("background-filename",
                          "Background Filename",
                          "The image filename of the background",
                          NULL,
                          G_PARAM_WRITABLE));
  /**
   * BigBox:background-pixbuf:
   *
   * Background pixbuf, covers padding but not border.
   */
  g_object_class_install_property
    (gobject_class,
     PROP_BACKGROUND_PIXBUF,
     g_param_spec_object ("background-pixbuf",
                          "Background Pixbuf",
                          "The image pixbuf of the background",
                          GDK_TYPE_PIXBUF,
                          G_PARAM_WRITABLE));
  /**
   * BigBox:background-texture:
   *
   * Background texture, covers padding but not border.
   */
  g_object_class_install_property
    (gobject_class,
     PROP_BACKGROUND_TEXTURE,
     g_param_spec_object ("background-texture",
                          "Background Texture",
                          "The texture of the background",
                          CLUTTER_TYPE_TEXTURE,
                          G_PARAM_READABLE));

  /**
   * BigBox:background-rectangle:
   *
   * Background rectangle, covers the complete box allocation.
   */
  g_object_class_install_property
    (gobject_class,
     PROP_BACKGROUND_RECTANGLE,
     g_param_spec_object ("background-rectangle",
                          "Background Rectangle",
                          "The rectangle forming the box border and "
                          "background color",
                          BIG_TYPE_RECTANGLE,
                          G_PARAM_READABLE));
  /**
   * BigBox:background-repeat:
   *
   * Sets if/how a background image will be repeated.
   */
  g_object_class_install_property
                 (gobject_class,
                  PROP_BACKGROUND_REPEAT,
                  g_param_spec_enum ("background-repeat",
                                     "Background Repeat",
                                     "Background Repeat",
                                     BIG_TYPE_BOX_BACKGROUND_REPEAT,
                                     BIG_BOX_BACKGROUND_REPEAT_NONE,
                                     G_PARAM_READWRITE));
  /**
   * BigBox:background-x-align:
   *
   * Sets the horizontal alignment of the background texture.
   */
  g_object_class_install_property
                 (gobject_class,
                  PROP_BACKGROUND_X_ALIGN,
                  g_param_spec_enum ("background-x-align",
                                     "Background X alignment",
                                     "Background X alignment",
                                     BIG_TYPE_BOX_ALIGNMENT,
                                     BIG_BOX_ALIGNMENT_FILL,
                                     G_PARAM_READWRITE));
  /**
   * BigBox:background-y-align:
   *
   * Sets the vertical alignment of the background texture.
   */
  g_object_class_install_property
                 (gobject_class,
                  PROP_BACKGROUND_Y_ALIGN,
                  g_param_spec_enum ("background-y-align",
                                     "Background Y alignment",
                                     "Background Y alignment",
                                     BIG_TYPE_BOX_ALIGNMENT,
                                     BIG_BOX_ALIGNMENT_FILL,
                                     G_PARAM_READWRITE));
  /**
   * BigBox:border-color:
   *
   * Border color, color of border if any. Make the border transparent
   * to use the border purely for spacing with no visible border.
   *
   */
  g_object_class_install_property
    (gobject_class,
     PROP_BORDER_COLOR,
     g_param_spec_boxed ("border-color",
                         "Border Color",
                         "The color of the border of the rectangle",
                         CLUTTER_TYPE_COLOR,
                         G_PARAM_READWRITE));

  g_object_class_install_property
                 (gobject_class,
                  PROP_DEBUG,
                  g_param_spec_boolean ("debug",
                                        "Debug",
                                        "Whether debug is activated or not",
                                        FALSE,
                                        G_PARAM_READWRITE));
}

static void
big_box_init (BigBox *box)
{
  box->priv = BIG_BOX_GET_PRIVATE (box);

  box->priv->orientation = BIG_BOX_ORIENTATION_VERTICAL;
  box->priv->x_align = BIG_BOX_ALIGNMENT_FILL;
  box->priv->y_align = BIG_BOX_ALIGNMENT_FILL;
  box->priv->spacing = 0;
  box->priv->padding_top = 0;
  box->priv->padding_bottom = 0;
  box->priv->padding_left = 0;
  box->priv->padding_right = 0;
  box->priv->border_top = 0;
  box->priv->border_bottom = 0;
  box->priv->border_left = 0;
  box->priv->border_right = 0;

  box->priv->background_texture = NULL;
  box->priv->background_repeat = BIG_BOX_BACKGROUND_REPEAT_NONE;
  box->priv->background_x_align = BIG_BOX_ALIGNMENT_FILL;
  box->priv->background_y_align = BIG_BOX_ALIGNMENT_FILL;

  box->priv->background_rectangle = NULL;
  box->priv->draw_rounded_corner = FALSE;

  /* both bg and border colors default to black and transparent (all bits 0) */
}

ClutterActor *
big_box_new (BigBoxOrientation orientation)
{
  return g_object_new (BIG_TYPE_BOX,
                       "orientation", orientation,
                       NULL);
}

void
big_box_prepend (BigBox          *box,
                 ClutterActor    *child,
                 BigBoxPackFlags  flags)
{
  BigBoxPrivate *priv;
  BigBoxChild *c;

  g_return_if_fail (BIG_IS_BOX (box));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));

  priv = box->priv;

  g_object_ref (child);

  c = box_child_new_from_actor (child, flags);

  priv->children = g_list_prepend (priv->children, c);

  clutter_actor_set_parent (child, CLUTTER_ACTOR (box));

  g_signal_emit_by_name (box, "actor-added", child);

  clutter_actor_queue_relayout (CLUTTER_ACTOR (box));

  g_object_unref (child);
}

void
big_box_append (BigBox          *box,
                ClutterActor    *child,
                BigBoxPackFlags  flags)
{
  BigBoxPrivate *priv;
  BigBoxChild *c;

  g_return_if_fail (BIG_IS_BOX (box));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));

  priv = box->priv;

  g_object_ref (child);

  c = box_child_new_from_actor (child, flags);

  priv->children = g_list_append (priv->children, c);

  clutter_actor_set_parent (child, CLUTTER_ACTOR (box));

  g_signal_emit_by_name (box, "actor-added", child);

  big_box_real_sort_depth_order (CLUTTER_CONTAINER (box));

  clutter_actor_queue_relayout (CLUTTER_ACTOR (box));

  g_object_unref (child);
}

gboolean
big_box_is_empty (BigBox *box)
{
  g_return_val_if_fail (BIG_IS_BOX (box), TRUE);

  return (box->priv->children == NULL);
}

void
big_box_remove_all (BigBox *box)
{
  BigBoxPrivate *priv;

  g_return_if_fail (BIG_IS_BOX (box));

  priv = box->priv;

  while (priv->children != NULL) {
    BigBoxChild *child = priv->children->data;

    box_child_remove (box, child);
  }

  clutter_actor_queue_relayout (CLUTTER_ACTOR (box));
}

void
big_box_insert_after (BigBox          *box,
                      ClutterActor    *child,
                      ClutterActor    *ref_child,
                      BigBoxPackFlags  flags)
{
  BigBoxPrivate *priv;
  BigBoxChild *c, *ref_c;
  gint position;

  g_return_if_fail (BIG_IS_BOX (box));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));
  g_return_if_fail (CLUTTER_IS_ACTOR (ref_child));

  priv = box->priv;

  g_object_ref (child);

  ref_c = box_child_find (box, ref_child);

  if (ref_c != NULL)
    {
      c = box_child_new_from_actor (child, flags);

      position = g_list_index (priv->children, ref_c);
      priv->children = g_list_insert (priv->children, c, ++position);

      clutter_actor_set_parent (child, CLUTTER_ACTOR (box));

      g_signal_emit_by_name (box, "actor-added", child);

      clutter_actor_queue_relayout (CLUTTER_ACTOR (box));
    }

  g_object_unref (child);
}

void
big_box_insert_before (BigBox          *box,
                       ClutterActor    *child,
                       ClutterActor    *ref_child,
                       BigBoxPackFlags  flags)
{
  BigBoxPrivate *priv;
  BigBoxChild *c, *ref_c;
  gint position;

  g_return_if_fail (BIG_IS_BOX (box));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));

  priv = box->priv;

  g_object_ref (child);

  ref_c = box_child_find (box, ref_child);

  if (ref_c != NULL)
    {
      c = box_child_new_from_actor (child, flags);

      position = g_list_index (priv->children, ref_c);
      priv->children = g_list_insert (priv->children, c, position);

      clutter_actor_set_parent (child, CLUTTER_ACTOR (box));

      g_signal_emit_by_name (box, "actor-added", child);

      clutter_actor_queue_relayout (CLUTTER_ACTOR (box));
    }

  g_object_unref (child);
}

void
big_box_set_child_packing (BigBox          *box,
                           ClutterActor    *child,
                           BigBoxPackFlags  flags)
{
  BigBoxChild *c;

  g_return_if_fail (BIG_IS_BOX (box));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));

  g_object_ref (child);

  c = box_child_find (box, child);

  if (c != NULL && box_child_set_flags (c, flags))
    clutter_actor_queue_relayout (CLUTTER_ACTOR (box));

  g_object_unref (child);
}

void
big_box_set_child_align(BigBox              *box,
                        ClutterActor        *child,
                        BigBoxAlignment      fixed_x_align,
                        BigBoxAlignment      fixed_y_align)
{
  BigBoxChild *c;

  g_return_if_fail (BIG_IS_BOX (box));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));

  g_object_ref (child);

  c = box_child_find (box, child);

  if (c != NULL && box_child_set_align (c, fixed_x_align, fixed_y_align))
    clutter_actor_queue_relayout (CLUTTER_ACTOR (box));

  g_object_unref (child);
}

void
big_box_set_padding (BigBox *box, int padding)
{
  BigBoxPrivate *priv;
  ClutterUnit padding_in_units;
  gboolean padding_changed;

  g_return_if_fail (BIG_IS_BOX (box));
  g_return_if_fail (padding >= 0);

  priv = box->priv;

  padding_in_units = CLUTTER_UNITS_FROM_DEVICE (padding);

  padding_changed = (priv->padding_top    != padding_in_units ||
                     priv->padding_bottom != padding_in_units ||
                     priv->padding_left   != padding_in_units ||
                     priv->padding_right  != padding_in_units);

  if (padding_changed)
    {
      g_object_freeze_notify (G_OBJECT (box));

      if (box->priv->padding_top != padding_in_units)
        g_object_notify (G_OBJECT (box), "padding-top");
      box->priv->padding_top = padding_in_units;

      if (box->priv->padding_bottom != padding_in_units)
        g_object_notify (G_OBJECT (box), "padding-bottom");
      box->priv->padding_bottom = padding_in_units;

      if (box->priv->padding_left != padding_in_units)
        g_object_notify (G_OBJECT (box), "padding-left");
      box->priv->padding_left = padding_in_units;

      if (box->priv->padding_right != padding_in_units)
        g_object_notify (G_OBJECT (box), "padding-right");
      box->priv->padding_right = padding_in_units;

      g_object_thaw_notify (G_OBJECT (box));

      clutter_actor_queue_relayout (CLUTTER_ACTOR (box));
    }
}

void
big_box_set_border_width (BigBox *box, int border_width)
{
  BigBoxPrivate *priv;
  ClutterUnit border_in_units;
  gboolean border_changed;

  g_return_if_fail (BIG_IS_BOX (box));
  g_return_if_fail (border_width >= 0);

  priv = box->priv;

  border_in_units = CLUTTER_UNITS_FROM_DEVICE (border_width);

  border_changed = (priv->border_top    != border_in_units ||
                    priv->border_bottom != border_in_units ||
                    priv->border_left   != border_in_units ||
                    priv->border_right  != border_in_units);

  if (border_changed)
    {
      g_object_freeze_notify (G_OBJECT (box));

      if (box->priv->border_top != border_in_units)
        g_object_notify (G_OBJECT (box), "border-top");
      box->priv->border_top = border_in_units;

      if (box->priv->border_bottom != border_in_units)
        g_object_notify (G_OBJECT (box), "border-bottom");
      box->priv->border_bottom = border_in_units;

      if (box->priv->border_left != border_in_units)
        g_object_notify (G_OBJECT (box), "border-left");
      box->priv->border_left = border_in_units;

      if (box->priv->border_right != border_in_units)
        g_object_notify (G_OBJECT (box), "border-right");
      box->priv->border_right = border_in_units;

      g_object_thaw_notify (G_OBJECT (box));

      clutter_actor_queue_relayout (CLUTTER_ACTOR (box));

      big_box_update_draw_rounded_corner (box);
    }
}
