#include "graphics.h"
#include "network.h"

void graphics_draw_media_tab(void)
{
    C2D_Text textObj;

    graphics_draw_dynamic_text(&textObj, "MEDIA CONTROLS", 10, 8, 0.6f, C2D_Color32(60, 220, 220, 255));
    graphics_draw_hud_panel(10, 38, 250, 40, C2D_Color32(18, 24, 32, 255), C2D_Color32(60, 220, 220, 255), 10);
    if (g_now_playing[0])
        graphics_draw_dynamic_text(&textObj, g_now_playing, 18, 50, 0.5f, C2D_Color32(230, 230, 220, 255));
    else
        graphics_draw_dynamic_text(&textObj, "NO MEDIA PLAYING", 18, 50, 0.45f, C2D_Color32(140, 150, 150, 255));

    graphics_draw_hud_panel(20, 98, 60, 40, C2D_Color32(18, 24, 32, 255), C2D_Color32(28, 34, 40, 255), 8);
    graphics_draw_dynamic_text(&textObj, "<<", 38, 108, 0.6f, C2D_Color32(230, 230, 220, 255));
    graphics_draw_hud_panel(100, 98, 70, 40, C2D_Color32(18, 24, 32, 255), C2D_Color32(255, 160, 40, 255), 10);
    graphics_draw_dynamic_text(&textObj, "PLAY", 118, 108, 0.55f, C2D_Color32(255, 160, 40, 255));
    graphics_draw_hud_panel(190, 98, 60, 40, C2D_Color32(18, 24, 32, 255), C2D_Color32(28, 34, 40, 255), 8);
    graphics_draw_dynamic_text(&textObj, ">>", 208, 108, 0.6f, C2D_Color32(230, 230, 220, 255));
}
