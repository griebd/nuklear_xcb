/*****************************************************************************
 *
 * Nuklear XCB/Cairo Render Backend - v0.0.1
 *
 * Copyright 2017 Adriano Grieb
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 ****************************************************************************/

/*****************************************************************************
 *
 *                                API
 *
 ****************************************************************************/

#ifndef NK_XCB_H
#define NK_XCB_H

struct nk_xcb_context;

NK_API struct nk_xcb_context *nk_xcb_init (struct nk_color *, char *, int,
        int, int, int);
NK_API struct nk_user_font *nk_xcb_default_font(struct nk_xcb_context *);
NK_API void nk_xcb_set_nk_context (struct nk_xcb_context *,
        struct nk_context *);
NK_API void nk_xcb_free (struct nk_xcb_context *);
NK_API int nk_xcb_handle_event (struct nk_xcb_context *);
NK_API void nk_xcb_render (struct nk_xcb_context *);
NK_API void nk_xcb_size (struct nk_xcb_context *, int *, int *);

#endif /* NK_XCB_H */

/*****************************************************************************
 *
 *                           IMPLEMENTATION
 *
 ****************************************************************************/

#ifdef NK_XCB_IMPLEMENTATION

#include <xcb/xcb.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>
#include <cairo-xcb.h>
#include <cairo-ft.h>

#if defined _XOPEN_SOURCE && _XOPEN_SOURCE >= 600 || \
    defined _POSIX_C_SOURCE && _POSIX_C_SOURCE >= 200112L
#include <time.h>
#include <errno.h>
#ifndef NK_XCB_FPS
#define NK_XCB_FPS 30
#endif /* NK_XCB_FPS */
#define NK_XCB_NSEC 1000000000
#define NK_XCB_MIN_FRAME_TIME (NK_XCB_NSEC / NK_XCB_FPS)
#endif

#include <math.h>
#ifdef __USE_GNU
#define NK_XCB_PI M_PIl
#elif defined __USE_BSD || defined __USE_XOPEN
#define NK_XCB_PI M_PI
#else
#define NK_XCB_PI acos(-1.0)
#endif

#define NK_XCB_TO_CAIRO(x) ((double) x / 255.0)
#define NK_XCB_DEG_TO_RAD(x) ((double) x * NK_XCB_PI / 180.0)

struct nk_xcb_context {
    struct nk_context *nk_ctx;
    struct nk_user_font *font;
    xcb_connection_t *conn;
    xcb_key_symbols_t *key_symbols;
    cairo_surface_t *surface;
    cairo_t *cr;
    void *last_buffer;
    nk_size buffer_size;
    int repaint;
#ifdef NK_XCB_MIN_FRAME_TIME
    unsigned long last_render;
#endif /* NK_XCB_MIN_FRAME_TIME */
    struct nk_color *bg;
    xcb_intern_atom_reply_t* del_atom;
    int width, height;
};

NK_INTERN float nk_xcb_text_width (nk_handle handle,
        float height __attribute__ ((__unused__)), const char *text, int len)
{
    cairo_scaled_font_t *font = handle.ptr;
    cairo_glyph_t *glyphs = NULL;
    int num_glyphs;
    cairo_text_extents_t extents;

    cairo_scaled_font_text_to_glyphs (font, 0, 0, text, len, &glyphs,
            &num_glyphs, NULL, NULL, NULL);
    cairo_scaled_font_glyph_extents (font, glyphs, num_glyphs, &extents);
    cairo_glyph_free (glyphs);

    return extents.x_advance;
}

NK_API struct nk_xcb_context* nk_xcb_init (struct nk_color *bg,
        char *font_file, int pos_x, int pos_y, int width, int height)
{
    int screenNum;
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_window_t window;
    uint32_t values[1];
    xcb_visualtype_t *visual;
    cairo_surface_t *surface;
    cairo_t *cr;
    struct nk_user_font *font;
    cairo_scaled_font_t *default_font;
    cairo_font_extents_t extents;
    struct nk_xcb_context *xcb_ctx;
    xcb_intern_atom_cookie_t cookie;
    xcb_intern_atom_reply_t *reply, *del_atom;

    conn = xcb_connect (NULL, &screenNum);
    if (xcb_connection_has_error (conn)) {
        xcb_disconnect (conn);
        return NULL;
    }
    screen = xcb_aux_get_screen (conn, screenNum);

    window = xcb_generate_id (conn);
    values[0] = XCB_EVENT_MASK_KEY_PRESS
        | XCB_EVENT_MASK_KEY_RELEASE
        | XCB_EVENT_MASK_BUTTON_PRESS
        | XCB_EVENT_MASK_BUTTON_RELEASE
        /*| XCB_EVENT_MASK_ENTER_WINDOW*/
        /*| XCB_EVENT_MASK_LEAVE_WINDOW*/
        | XCB_EVENT_MASK_POINTER_MOTION
        /*| XCB_EVENT_MASK_POINTER_MOTION_HINT*/
        | XCB_EVENT_MASK_BUTTON_1_MOTION
        | XCB_EVENT_MASK_BUTTON_2_MOTION
        | XCB_EVENT_MASK_BUTTON_3_MOTION
        | XCB_EVENT_MASK_BUTTON_4_MOTION
        | XCB_EVENT_MASK_BUTTON_5_MOTION
        | XCB_EVENT_MASK_BUTTON_MOTION
        | XCB_EVENT_MASK_KEYMAP_STATE
        | XCB_EVENT_MASK_EXPOSURE
        /*| XCB_EVENT_MASK_VISIBILITY_CHANGE*/
        | XCB_EVENT_MASK_STRUCTURE_NOTIFY
        /*| XCB_EVENT_MASK_RESIZE_REDIRECT*/
        /*| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY*/
        /*| XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT*/
        /*| XCB_EVENT_MASK_FOCUS_CHANGE*/
        /*| XCB_EVENT_MASK_PROPERTY_CHANGE*/
        /*| XCB_EVENT_MASK_COLOR_MAP_CHANGE*/
        /*| XCB_EVENT_MASK_OWNER_GRAB_BUTTON*/;
    xcb_create_window (conn, XCB_COPY_FROM_PARENT, window, screen->root,
            pos_x, pos_y, width, height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
            XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, values);

    visual = xcb_aux_get_visualtype (conn, screenNum, screen->root_visual);
    surface = cairo_xcb_surface_create (conn, window, visual, width, height);
    cr = cairo_create (surface);

    font = malloc (sizeof (struct nk_user_font));
    if (font_file != NULL) {
        FT_Library library;
        FT_Face face;
        cairo_font_face_t *font_face;
        static const cairo_user_data_key_t key;

        FT_Init_FreeType (&library);
        FT_New_Face (library, font_file, 0, &face);
        font_face = cairo_ft_font_face_create_for_ft_face (face, 0);
        cairo_font_face_set_user_data (font_face, &key, face,
                (cairo_destroy_func_t) FT_Done_Face);
        cairo_set_font_face (cr, font_face);
    }
    cairo_set_font_size (cr, 11);
    default_font = cairo_get_scaled_font (cr);
    cairo_scaled_font_extents (default_font, &extents);
    font->userdata.ptr = default_font;
    font->height = extents.height;
    font->width = nk_xcb_text_width;

    cookie = xcb_intern_atom(conn, 1, 12, "WM_PROTOCOLS");
    reply = xcb_intern_atom_reply(conn, cookie, 0);
    cookie = xcb_intern_atom(conn, 0, 16, "WM_DELETE_WINDOW");
    del_atom = xcb_intern_atom_reply(conn, cookie, 0);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window, reply->atom, 4,
            32, 1, &del_atom->atom);
    free (reply);

    xcb_map_window (conn, window);
    xcb_flush (conn);

    xcb_ctx = malloc (sizeof (struct nk_xcb_context));
    xcb_ctx->conn = conn;
    xcb_ctx->surface = surface;
    xcb_ctx->key_symbols = xcb_key_symbols_alloc (xcb_ctx->conn);
    xcb_ctx->cr = cr;
    xcb_ctx->font = font;
    xcb_ctx->last_buffer = NULL;
    xcb_ctx->buffer_size = 0;
    xcb_ctx->repaint = nk_false;
    xcb_ctx->bg = bg;
    xcb_ctx->del_atom = del_atom;
    xcb_ctx->width = width;
    xcb_ctx->height = height;

    return xcb_ctx;
}

NK_API struct nk_user_font *nk_xcb_default_font(
        struct nk_xcb_context *xcb_ctx)
{
    return xcb_ctx->font;
}

NK_API void nk_xcb_set_nk_context (struct nk_xcb_context *xcb_ctx,
        struct nk_context *nk_ctx)
{
    xcb_ctx->nk_ctx = nk_ctx;
}

NK_API void nk_xcb_free (struct nk_xcb_context *xcb_ctx)
{
    free (xcb_ctx->last_buffer);
    cairo_destroy (xcb_ctx->cr);
    cairo_surface_destroy (xcb_ctx->surface);
    free (xcb_ctx->font);
    free (xcb_ctx->del_atom);
    xcb_key_symbols_free (xcb_ctx->key_symbols);
    xcb_disconnect (xcb_ctx->conn);
    free (xcb_ctx);
}

NK_API int nk_xcb_handle_event (struct nk_xcb_context *xcb_ctx)
{
#ifdef NK_XCB_MIN_FRAME_TIME
    struct timespec tp;

    clock_gettime(CLOCK_MONOTONIC_COARSE, &tp);
    xcb_ctx->last_render = tp.tv_sec * NK_XCB_NSEC + tp.tv_nsec;
#endif /* NK_XCB_MIN_FRAME_TIME */

    xcb_generic_event_t *event = xcb_wait_for_event (xcb_ctx->conn);

    nk_input_begin (xcb_ctx->nk_ctx);
    do {
        switch (XCB_EVENT_RESPONSE_TYPE(event)) {
            case XCB_KEY_PRESS:
            case XCB_KEY_RELEASE:
                {
                    int press =
                        (XCB_EVENT_RESPONSE_TYPE(event)) == XCB_KEY_PRESS;
                    xcb_key_press_event_t *kp =
                        (xcb_key_press_event_t *) event;
                    xcb_keysym_t sym = xcb_key_symbols_get_keysym (
                            xcb_ctx->key_symbols, kp->detail, kp->state);
                    switch (sym) {
                        case XK_Shift_L:
                        case XK_Shift_R:
                            nk_input_key (xcb_ctx->nk_ctx, NK_KEY_SHIFT,
                                    press);
                            break;
                        case XK_Control_L:
                        case XK_Control_R:
                            nk_input_key (xcb_ctx->nk_ctx, NK_KEY_CTRL,
                                    press);
                            break;
                        case XK_Delete:
                            nk_input_key (xcb_ctx->nk_ctx, NK_KEY_DEL, press);
                            break;
                        case XK_Return:
                            nk_input_key (xcb_ctx->nk_ctx, NK_KEY_ENTER,
                                    press);
                            break;
                        case XK_Tab:
                            nk_input_key (xcb_ctx->nk_ctx, NK_KEY_TAB, press);
                            break;
                        case XK_BackSpace:
                            nk_input_key (xcb_ctx->nk_ctx, NK_KEY_BACKSPACE,
                                    press);
                            break;
                        /* case NK_KEY_COPY */
                        /* case NK_KEY_CUT */
                        /* case NK_KEY_PASTE */
                        case XK_Up:
                            nk_input_key (xcb_ctx->nk_ctx, NK_KEY_UP, press);
                            break;
                        case XK_Down:
                            nk_input_key (xcb_ctx->nk_ctx, NK_KEY_DOWN,
                                    press);
                            break;
                        case XK_Left:
                            nk_input_key (xcb_ctx->nk_ctx, NK_KEY_LEFT,
                                    press);
                            break;
                        case XK_Right:
                            nk_input_key (xcb_ctx->nk_ctx, NK_KEY_RIGHT,
                                    press);
                            break;
                        /* NK_KEY_TEXT_INSERT_MODE, */
                        /* NK_KEY_TEXT_REPLACE_MODE, */
                        case XK_Escape:
                            nk_input_key (xcb_ctx->nk_ctx,
                                    NK_KEY_TEXT_RESET_MODE, press);
                            break;
                        /* NK_KEY_TEXT_LINE_START, */
                        /* NK_KEY_TEXT_LINE_END, */
                        case XK_Home:
                            {
                                nk_input_key (xcb_ctx->nk_ctx,
                                        NK_KEY_TEXT_START, press);
                                nk_input_key (xcb_ctx->nk_ctx,
                                        NK_KEY_SCROLL_START, press);
                            }
                            break;
                        case XK_End:
                            {
                                nk_input_key (xcb_ctx->nk_ctx,
                                        NK_KEY_TEXT_END, press);
                                nk_input_key (xcb_ctx->nk_ctx,
                                        NK_KEY_SCROLL_END, press);
                            }
                            break;
                        /* NK_KEY_TEXT_UNDO, */
                        /* NK_KEY_TEXT_REDO, */
                        /* NK_KEY_TEXT_SELECT_ALL, */
                        /* NK_KEY_TEXT_WORD_LEFT, */
                        /* NK_KEY_TEXT_WORD_RIGHT, */
                        case XK_Page_Down:
                            nk_input_key (xcb_ctx->nk_ctx, NK_KEY_SCROLL_DOWN,
                                    press);
                            break;
                        case XK_Page_Up:
                            nk_input_key (xcb_ctx->nk_ctx, NK_KEY_SCROLL_UP,
                                    press);
                            break;
                        default:
                            if (press &&
                                    ! xcb_is_keypad_key (sym) &&
                                    ! xcb_is_private_keypad_key (sym) &&
                                    ! xcb_is_cursor_key (sym) &&
                                    ! xcb_is_pf_key (sym) &&
                                    ! xcb_is_function_key (sym) &&
                                    ! xcb_is_misc_function_key (sym) &&
                                    ! xcb_is_modifier_key (sym)
                                    ) {
                                nk_input_char (xcb_ctx->nk_ctx, sym);
                            } else
                                printf("state: %x code: %x sum: %x\n", kp->state, kp->detail, sym);
                            break;
                    }
                }
                break;
            case XCB_BUTTON_PRESS:
            case XCB_BUTTON_RELEASE:
                {
                    int press =
                        (XCB_EVENT_RESPONSE_TYPE(event)) == XCB_BUTTON_PRESS;
                    xcb_button_press_event_t *bp =
                        (xcb_button_press_event_t *) event;
                    switch (bp->detail) {
                        case XCB_BUTTON_INDEX_1:
                            nk_input_button (xcb_ctx->nk_ctx, NK_BUTTON_LEFT,
                                    bp->event_x, bp->event_y, press);
                            break;
                        case XCB_BUTTON_INDEX_2:
                            nk_input_button (xcb_ctx->nk_ctx,
                                    NK_BUTTON_MIDDLE, bp->event_x,
                                    bp->event_y, press);
                            break;
                        case XCB_BUTTON_INDEX_3:
                            nk_input_button (xcb_ctx->nk_ctx, NK_BUTTON_RIGHT,
                                    bp->event_x, bp->event_y, press);
                            break;
                        case XCB_BUTTON_INDEX_4:
                            nk_input_scroll (xcb_ctx->nk_ctx,
                                    nk_vec2 (0, 1.0f));
                            break;
                        case XCB_BUTTON_INDEX_5:
                            nk_input_scroll (xcb_ctx->nk_ctx,
                                    nk_vec2 (0, -1.0f));
                            break;
                        default: break;
                    }
                }
                break;
            case XCB_MOTION_NOTIFY:
                {
                    xcb_motion_notify_event_t *mn =
                        (xcb_motion_notify_event_t *) event;
                    nk_input_motion (xcb_ctx->nk_ctx, mn->event_x,
                            mn->event_y);
                }
                break;
            case XCB_SELECTION_CLEAR:
                {
                    printf ("Unhandled event: %s\n", xcb_event_get_label(event->response_type));
                }
                break;
            case XCB_SELECTION_REQUEST:
                {
                    printf ("Unhandled event: %s\n", xcb_event_get_label(event->response_type));
                }
                break;
            case XCB_SELECTION_NOTIFY:
                {
                    printf ("Unhandled event: %s\n", xcb_event_get_label(event->response_type));
                }
                break;
            case XCB_CONFIGURE_NOTIFY:
                {
                    xcb_configure_notify_event_t *cn =
                        (xcb_configure_notify_event_t *) event;
                    cairo_xcb_surface_set_size (xcb_ctx->surface, cn->width,
                            cn->height);
                    xcb_ctx->width = cn->width;
                    xcb_ctx->height = cn->height;
                }
                break;
            case XCB_KEYMAP_NOTIFY:
                xcb_refresh_keyboard_mapping (xcb_ctx->key_symbols,
                        (xcb_mapping_notify_event_t *) event);
                break;
            case XCB_EXPOSE:
            case XCB_REPARENT_NOTIFY:
            case XCB_MAP_NOTIFY:
                xcb_ctx->repaint = nk_true;
                break;
            case XCB_CLIENT_MESSAGE:
                {
                    xcb_client_message_event_t *cm =
                        (xcb_client_message_event_t *) event;
                    if (cm->data.data32[0] == xcb_ctx->del_atom->atom)
                    {
                        return 0;
                    }
                }
                break;
            default:
                printf ("Unhandled event: %s\n", xcb_event_get_label(event->response_type));
                break;
        }
        free (event);
    } while ((event = xcb_poll_for_event (xcb_ctx->conn)));
    nk_input_end (xcb_ctx->nk_ctx);

    return 1;
}

NK_API void nk_xcb_render (struct nk_xcb_context *xcb_ctx)
{
    cairo_t *cr;
    const struct nk_command *cmd = NULL;
    void *cmds = nk_buffer_memory (&xcb_ctx->nk_ctx->memory);

    if (xcb_ctx->buffer_size != xcb_ctx->nk_ctx->memory.allocated) {
        xcb_ctx->buffer_size = xcb_ctx->nk_ctx->memory.allocated;
        xcb_ctx->last_buffer = realloc (xcb_ctx->last_buffer,
                xcb_ctx->buffer_size);
        memcpy (xcb_ctx->last_buffer, cmds, xcb_ctx->buffer_size);
    } else if (! memcmp (cmds, xcb_ctx->last_buffer, xcb_ctx->buffer_size)) {
        if (!xcb_ctx->repaint) {
            return;
        }
        xcb_ctx->repaint = nk_false;
    } else {
        memcpy (xcb_ctx->last_buffer, cmds, xcb_ctx->buffer_size);
    }

    cr = xcb_ctx->cr;
    cairo_push_group (cr);

    cairo_set_source_rgb (cr, NK_XCB_TO_CAIRO(xcb_ctx->bg->r),
            NK_XCB_TO_CAIRO(xcb_ctx->bg->g), NK_XCB_TO_CAIRO(xcb_ctx->bg->b));
    cairo_paint (cr);

    nk_foreach (cmd, xcb_ctx->nk_ctx) {
        switch (cmd->type) {
            case NK_COMMAND_NOP:
                printf ("seriously\n");
                abort();
                break;
            case NK_COMMAND_SCISSOR:
                {
                    const struct nk_command_scissor *s =
                        (const struct nk_command_scissor *) cmd;
                    cairo_reset_clip (cr);
                    if (s->x >= 0) {
                        cairo_rectangle (cr, s->x - 1, s->y - 1, s->w + 2,
                                s->h + 2);
                        cairo_clip (cr);
                    }
                }
                break;
            case NK_COMMAND_LINE:
                {
                    const struct nk_command_line *l =
                        (const struct nk_command_line *) cmd;
                    cairo_set_source_rgba (cr, NK_XCB_TO_CAIRO(l->color.r),
                            NK_XCB_TO_CAIRO(l->color.g),
                            NK_XCB_TO_CAIRO(l->color.b),
                            NK_XCB_TO_CAIRO(l->color.a));
                    cairo_set_line_width (cr, l->line_thickness);
                    cairo_move_to (cr, l->begin.x, l->begin.y);
                    cairo_line_to (cr, l->end.x, l->end.y);
                    cairo_stroke (cr);
                }
                break;
            case NK_COMMAND_CURVE:
                {
                    const struct nk_command_curve *q =
                        (const struct nk_command_curve *) cmd;
                    cairo_set_source_rgba (cr, NK_XCB_TO_CAIRO(q->color.r),
                            NK_XCB_TO_CAIRO(q->color.g),
                            NK_XCB_TO_CAIRO(q->color.b),
                            NK_XCB_TO_CAIRO(q->color.a));
                    cairo_set_line_width (cr, q->line_thickness);
                    cairo_move_to (cr, q->begin.x, q->begin.y);
                    cairo_curve_to (cr, q->ctrl[0].x, q->ctrl[0].y,
                            q->ctrl[1].x, q->ctrl[1].y, q->end.x, q->end.y);
                    cairo_stroke (cr);
                }
                break;
            case NK_COMMAND_RECT:
                {
                    const struct nk_command_rect *r =
                        (const struct nk_command_rect *) cmd;
                    cairo_set_source_rgba (cr, NK_XCB_TO_CAIRO(r->color.r),
                            NK_XCB_TO_CAIRO(r->color.g),
                            NK_XCB_TO_CAIRO(r->color.b),
                            NK_XCB_TO_CAIRO(r->color.a));
                    cairo_set_line_width (cr, r->line_thickness);
                    if (r->rounding == 0) {
                        cairo_rectangle (cr, r->x, r->y, r->w, r->h);
                    } else {
                        int xl = r->x + r->w - r->rounding;
                        int xr = r->x + r->rounding;
                        int yl = r->y + r->h - r->rounding;
                        int yr = r->y + r->rounding;
                        cairo_new_sub_path (cr);
                        cairo_arc (cr, xl, yr, r->rounding,
                                NK_XCB_DEG_TO_RAD(-90), NK_XCB_DEG_TO_RAD(0));
                        cairo_arc (cr, xl, yl, r->rounding,
                                NK_XCB_DEG_TO_RAD(0), NK_XCB_DEG_TO_RAD(90));
                        cairo_arc (cr, xr, yl, r->rounding,
                                NK_XCB_DEG_TO_RAD(90),
                                NK_XCB_DEG_TO_RAD(180));
                        cairo_arc (cr, xr, yr, r->rounding,
                                NK_XCB_DEG_TO_RAD(180),
                                NK_XCB_DEG_TO_RAD(270));
                        cairo_close_path (cr);
                    }
                    cairo_stroke (cr);
                }
                break;
            case NK_COMMAND_RECT_FILLED:
                {
                    const struct nk_command_rect_filled *r =
                        (const struct nk_command_rect_filled *) cmd;
                    cairo_set_source_rgba (cr, NK_XCB_TO_CAIRO(r->color.r),
                            NK_XCB_TO_CAIRO(r->color.g),
                            NK_XCB_TO_CAIRO(r->color.b),
                            NK_XCB_TO_CAIRO(r->color.a));
                    if (r->rounding == 0) {
                        cairo_rectangle (cr, r->x, r->y, r->w, r->h);
                    } else {
                        int xl = r->x + r->w - r->rounding;
                        int xr = r->x + r->rounding;
                        int yl = r->y + r->h - r->rounding;
                        int yr = r->y + r->rounding;
                        cairo_new_sub_path (cr);
                        cairo_arc (cr, xl, yr, r->rounding,
                                NK_XCB_DEG_TO_RAD(-90), NK_XCB_DEG_TO_RAD(0));
                        cairo_arc (cr, xl, yl, r->rounding,
                                NK_XCB_DEG_TO_RAD(0), NK_XCB_DEG_TO_RAD(90));
                        cairo_arc (cr, xr, yl, r->rounding,
                                NK_XCB_DEG_TO_RAD(90),
                                NK_XCB_DEG_TO_RAD(180));
                        cairo_arc (cr, xr, yr, r->rounding,
                                NK_XCB_DEG_TO_RAD(180),
                                NK_XCB_DEG_TO_RAD(270));
                        cairo_close_path (cr);
                    }
                    cairo_fill (cr);
                }
                break;
            case NK_COMMAND_RECT_MULTI_COLOR:
                {
                    /* const struct nk_command_rect_multi_color *r =
                        (const struct nk_command_rect_multi_color *) cmd; */
                    /* TODO */
                }
                break;
            case NK_COMMAND_CIRCLE:
                {
                    const struct nk_command_circle *c =
                        (const struct nk_command_circle *) cmd;
                    cairo_set_source_rgba (cr, NK_XCB_TO_CAIRO(c->color.r),
                            NK_XCB_TO_CAIRO(c->color.g),
                            NK_XCB_TO_CAIRO(c->color.b),
                            NK_XCB_TO_CAIRO(c->color.a));
                    cairo_set_line_width (cr, c->line_thickness);
                    cairo_save (cr);
                    cairo_translate (cr, c->x + c->w / 2.0,
                            c->y + c->h / 2.0);
                    cairo_scale (cr, c->w / 2.0, c->h / 2.0);
                    cairo_arc (cr, 0, 0, 1, NK_XCB_DEG_TO_RAD(0),
                            NK_XCB_DEG_TO_RAD(360));
                    cairo_restore (cr);
                    cairo_stroke (cr);
                }
                break;
            case NK_COMMAND_CIRCLE_FILLED:
                {
                    const struct nk_command_circle_filled *c =
                        (const struct nk_command_circle_filled *) cmd;
                    cairo_set_source_rgba (cr, NK_XCB_TO_CAIRO(c->color.r),
                            NK_XCB_TO_CAIRO(c->color.g),
                            NK_XCB_TO_CAIRO(c->color.b),
                            NK_XCB_TO_CAIRO(c->color.a));
                    cairo_save (cr);
                    cairo_translate (cr, c->x + c->w / 2.0,
                            c->y + c->h / 2.0);
                    cairo_scale (cr, c->w / 2.0, c->h / 2.0);
                    cairo_arc (cr, 0, 0, 1, NK_XCB_DEG_TO_RAD(0),
                            NK_XCB_DEG_TO_RAD(360));
                    cairo_restore (cr);
                    cairo_fill (cr);
                }
                break;
            case NK_COMMAND_ARC:
                {
                    const struct nk_command_arc *a =
                        (const struct nk_command_arc*) cmd;
                    cairo_set_source_rgba (cr, NK_XCB_TO_CAIRO(a->color.r),
                            NK_XCB_TO_CAIRO(a->color.g),
                            NK_XCB_TO_CAIRO(a->color.b),
                            NK_XCB_TO_CAIRO(a->color.a));
                    cairo_set_line_width (cr, a->line_thickness);
                    cairo_arc (cr, a->cx, a->cy, a->r,
                            NK_XCB_DEG_TO_RAD(a->a[0]),
                            NK_XCB_DEG_TO_RAD(a->a[1]));
                    cairo_stroke (cr);
                }
                break;
            case NK_COMMAND_ARC_FILLED:
                {
                    const struct nk_command_arc_filled *a =
                        (const struct nk_command_arc_filled*) cmd;
                    cairo_set_source_rgba (cr, NK_XCB_TO_CAIRO(a->color.r),
                            NK_XCB_TO_CAIRO(a->color.g),
                            NK_XCB_TO_CAIRO(a->color.b),
                            NK_XCB_TO_CAIRO(a->color.a));
                    cairo_arc (cr, a->cx, a->cy, a->r,
                            NK_XCB_DEG_TO_RAD(a->a[0]),
                            NK_XCB_DEG_TO_RAD(a->a[1]));
                    cairo_fill (cr);
                }
                break;
            case NK_COMMAND_TRIANGLE:
                {
                    const struct nk_command_triangle *t =
                        (const struct nk_command_triangle *) cmd;
                    cairo_set_source_rgba (cr, NK_XCB_TO_CAIRO(t->color.r),
                            NK_XCB_TO_CAIRO(t->color.g),
                            NK_XCB_TO_CAIRO(t->color.b),
                            NK_XCB_TO_CAIRO(t->color.a));
                    cairo_set_line_width (cr, t->line_thickness);
                    cairo_move_to (cr, t->a.x, t->a.y);
                    cairo_line_to (cr, t->b.x, t->b.y);
                    cairo_line_to (cr, t->c.x, t->c.y);
                    cairo_close_path (cr);
                    cairo_stroke (cr);
                }
                break;
            case NK_COMMAND_TRIANGLE_FILLED:
                {
                    const struct nk_command_triangle_filled *t =
                        (const struct nk_command_triangle_filled *) cmd;
                    cairo_set_source_rgba (cr, NK_XCB_TO_CAIRO(t->color.r),
                            NK_XCB_TO_CAIRO(t->color.g),
                            NK_XCB_TO_CAIRO(t->color.b),
                            NK_XCB_TO_CAIRO(t->color.a));
                    cairo_move_to (cr, t->a.x, t->a.y);
                    cairo_line_to (cr, t->b.x, t->b.y);
                    cairo_line_to (cr, t->c.x, t->c.y);
                    cairo_close_path (cr);
                    cairo_fill (cr);
                }
                break;
            case NK_COMMAND_POLYGON:
                {
                    int i;
                    const struct nk_command_polygon *p =
                        (const struct nk_command_polygon *) cmd;

                    cairo_set_source_rgba (cr, NK_XCB_TO_CAIRO(p->color.r),
                            NK_XCB_TO_CAIRO(p->color.g),
                            NK_XCB_TO_CAIRO(p->color.b),
                            NK_XCB_TO_CAIRO(p->color.a));
                    cairo_set_line_width (cr, p->line_thickness);
                    cairo_move_to (cr, p->points[0].x, p->points[0].y);
                    for (i = 1; i < p->point_count; ++i) {
                        cairo_line_to (cr, p->points[i].x, p->points[i].y);
                    }
                    cairo_close_path (cr);
                    cairo_stroke (cr);
                }
                break;
            case NK_COMMAND_POLYGON_FILLED:
                {
                    int i;
                    const struct nk_command_polygon_filled *p =
                        (const struct nk_command_polygon_filled *) cmd;

                    cairo_set_source_rgba (cr, NK_XCB_TO_CAIRO(p->color.r),
                            NK_XCB_TO_CAIRO(p->color.g),
                            NK_XCB_TO_CAIRO(p->color.b),
                            NK_XCB_TO_CAIRO(p->color.a));
                    cairo_move_to (cr, p->points[0].x, p->points[0].y);
                    for (i = 1; i < p->point_count; ++i) {
                        cairo_line_to (cr, p->points[i].x, p->points[i].y);
                    }
                    cairo_close_path (cr);
                    cairo_fill (cr);
                }
                break;
            case NK_COMMAND_POLYLINE:
                {
                    int i;
                    const struct nk_command_polyline *p =
                        (const struct nk_command_polyline *) cmd;

                    cairo_set_source_rgba (cr, NK_XCB_TO_CAIRO(p->color.r),
                            NK_XCB_TO_CAIRO(p->color.g),
                            NK_XCB_TO_CAIRO(p->color.b),
                            NK_XCB_TO_CAIRO(p->color.a));
                    cairo_set_line_width (cr, p->line_thickness);
                    cairo_move_to (cr, p->points[0].x, p->points[0].y);
                    for (i = 1; i < p->point_count; ++i) {
                        cairo_line_to (cr, p->points[i].x, p->points[i].y);
                    }
                    cairo_stroke (cr);
                }
                break;
            case NK_COMMAND_TEXT:
                {
                    const struct nk_command_text *t =
                        (const struct nk_command_text *) cmd;
                    cairo_glyph_t *glyphs = NULL;
                    int num_glyphs;
                    cairo_text_cluster_t *clusters = NULL;
                    int num_clusters;
                    cairo_text_cluster_flags_t cluster_flags;
                    cairo_font_extents_t extents;

                    cairo_set_source_rgba (cr,
                            NK_XCB_TO_CAIRO(t->foreground.r),
                            NK_XCB_TO_CAIRO(t->foreground.g),
                            NK_XCB_TO_CAIRO(t->foreground.b),
                            NK_XCB_TO_CAIRO(t->foreground.a));
                    cairo_scaled_font_extents (t->font->userdata.ptr,
                            &extents);
                    cairo_scaled_font_text_to_glyphs (t->font->userdata.ptr,
                            t->x, t->y + extents.ascent, t->string, t->length,
                            &glyphs, &num_glyphs, &clusters, &num_clusters,
                            &cluster_flags);
                    cairo_show_text_glyphs (cr, t->string, t->length, glyphs,
                            num_glyphs, clusters, num_clusters,
                            cluster_flags);
                    cairo_glyph_free (glyphs);
                    cairo_text_cluster_free (clusters);
                }
                break;
            case NK_COMMAND_IMAGE:
                {
                    printf ("TODO: NK_COMMAND_IMAGE\n");
                    /* TODO */
                }
                break;
            case NK_COMMAND_CUSTOM:
                {
                    printf ("TODO: NK_COMMAND_CUSTOM\n");
                    /* TODO */
                }
            default: break;
        }
    }

    cairo_pop_group_to_source (cr);
    cairo_paint (cr);
    cairo_surface_flush (xcb_ctx->surface);
    xcb_flush (xcb_ctx->conn);

#ifdef NK_XCB_MIN_FRAME_TIME
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &tp);
    unsigned long spent = tp.tv_sec * NK_XCB_NSEC + tp.tv_nsec -
        xcb_ctx->last_render;
    if (NK_XCB_MIN_FRAME_TIME > spent) {
        tp.tv_sec = 0;
        tp.tv_nsec = NK_XCB_MIN_FRAME_TIME - spent;
        while (clock_nanosleep(CLOCK_MONOTONIC, 0, &tp, &tp) == EINTR);
    }
#endif /* NK_XCB_MIN_FRAME_TIME */
}

NK_API void nk_xcb_size (struct nk_xcb_context *xcb_ctx, int *width, int *height)
{
    *width = xcb_ctx->width;
    *height = xcb_ctx->height;
}

#endif /* NK_XCB_IMPLEMENTATION */
