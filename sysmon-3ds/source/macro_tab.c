#include "graphics.h"
#include "network.h"
#include <stdio.h>
#include <string.h>

void graphics_draw_macro_tab(void)
{
    C2D_Text textObj;

    graphics_draw_dynamic_text(&textObj, "MACRO DECK", 10, 8, 0.6f, C2D_Color32(255, 160, 40, 255));
    graphics_draw_dynamic_text(&textObj, "Tap to execute macro", 10, 28, 0.4f, C2D_Color32(140, 150, 150, 255));

    int cols = 2;
    int btn_w = 120;
    int btn_h = 40;
    int start_x = 10;
    int start_y = 55;
    for (int i = 0; i < g_macro_count; i++)
    {
        int row = i / cols;
        int col = i % cols;
        int bx = start_x + (col * (btn_w + 10));
        int by = start_y + (row * (btn_h + 10));
        u32 accent = C2D_Color32(255, 160, 40, 255);
        if (strstr(g_macros[i].color, "blue"))
            accent = C2D_Color32(60, 220, 220, 255);
        else if (strstr(g_macros[i].color, "red"))
            accent = C2D_Color32(220, 60, 40, 255);
        else if (strstr(g_macros[i].color, "green"))
            accent = C2D_Color32(60, 220, 220, 255);
        else if (strstr(g_macros[i].color, "purple"))
            accent = C2D_Color32(255, 160, 40, 255);
        graphics_draw_hud_panel(bx, by, btn_w, btn_h, C2D_Color32(18, 24, 32, 255), accent, 10);
        graphics_draw_dynamic_text(&textObj, g_macros[i].label, bx + 8, by + 12, 0.4f, C2D_Color32(230, 230, 220, 255));
    }
}
