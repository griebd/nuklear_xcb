#include "nuklear_cfg.h"
#include "../nuklear_xcb.h"

#include <stdlib.h>

int main (void) {
    struct nk_context ctx;
    struct nk_xcb_context *xcb_ctx;
    struct nk_color background;

    background = nk_rgb (0, 0, 0);
    xcb_ctx = nk_xcb_init (&background, NULL, 20, 20, 600, 500);
    nk_init_default (&ctx, nk_xcb_default_font (xcb_ctx));
    nk_xcb_set_nk_context (xcb_ctx, &ctx);

    while (1)
    {
        int w, h;

        if (!nk_xcb_handle_event (xcb_ctx))
        {
            break;
        }

        nk_xcb_size (xcb_ctx, &w, &h);
        if (nk_begin (&ctx, "Menu", nk_rect (0, 0, w, h),
                    NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND))
        {
            nk_menubar_begin (&ctx);
            nk_layout_row_begin (&ctx, NK_STATIC, 25, 3);
            nk_layout_row_push (&ctx, 45);
            if (nk_menu_begin_label (&ctx, "File", NK_TEXT_LEFT, nk_vec2 (120, 200)))
            {
                nk_layout_row_dynamic (&ctx, 25, 1);
                if (nk_menu_item_label (&ctx, "Exit", NK_TEXT_LEFT))
                {
                    nk_window_close (&ctx, "Menu");
                }
                nk_menu_end (&ctx);
            }
            nk_menubar_end (&ctx);
        }
        nk_end (&ctx);
        if (nk_window_is_hidden (&ctx, "Menu"))
        {
            break;
        }

        nk_xcb_render (xcb_ctx);
        nk_clear (&ctx);
    }

    nk_xcb_free (xcb_ctx);
    nk_free (&ctx);

    return EXIT_SUCCESS;
}
