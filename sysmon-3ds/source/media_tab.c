#include "graphics.h"
#include "network.h"

void graphics_draw_media_tab(void)
{
    const ThemeColors *tc = graphics_get_colors();
    C2D_Text textObj;

    graphics_draw_dynamic_text(&textObj, "MEDIA CONTROLS", 10, 8, 0.6f, tc->cyan);
    graphics_draw_hud_panel(10, 38, 250, 40, tc->panel, tc->cyan, 10);

    if (g_now_playing[0])
    {
        size_t len = strlen(g_now_playing);
        if (len <= 24)
        {
            graphics_draw_dynamic_text(&textObj, g_now_playing, 18, 50, 0.45f, tc->text);
        }
        else
        {
            char line1[64];
            char line2[64];
            snprintf(line1, sizeof(line1), "%.24s", g_now_playing);
            if (len > 46)
                snprintf(line2, sizeof(line2), "%.20s...", g_now_playing + 24);
            else
                snprintf(line2, sizeof(line2), "%.24s", g_now_playing + 24);

            graphics_draw_dynamic_text(&textObj, line1, 18, 43, 0.38f, tc->text);
            graphics_draw_dynamic_text(&textObj, line2, 18, 58, 0.38f, tc->textDim);
        }
    }
    else
    {
        graphics_draw_dynamic_text(&textObj, "NO MEDIA PLAYING", 18, 50, 0.45f, tc->textDim);
    }

    // Prev
    graphics_draw_hud_panel(20, 98, 60, 40, tc->panel, tc->panelDim, 8);
    graphics_draw_dynamic_text(&textObj, "<<", 38, 108, 0.6f, tc->text);

    // Play/Pause (amber accent)
    graphics_draw_hud_panel(100, 98, 70, 40, tc->panel, tc->amber, 10);
    graphics_draw_dynamic_text(&textObj, "PLAY", 118, 108, 0.55f, tc->amber);

    // Next
    graphics_draw_hud_panel(190, 98, 60, 40, tc->panel, tc->panelDim, 8);
    graphics_draw_dynamic_text(&textObj, ">>", 208, 108, 0.6f, tc->text);
}
