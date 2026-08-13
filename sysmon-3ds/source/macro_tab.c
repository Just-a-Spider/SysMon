#include "graphics.h"
#include "network.h"
#include <stdio.h>
#include <string.h>

// Grid layout — shared with touch-hit path in main.c
#define MACRO_COLS      2
#define MACRO_AREA_TOP  50
#define MACRO_AREA_H    188
#define MACRO_GAP       6
#define MACRO_BTN_W     ((260 - MACRO_AREA_TOP/10 - MACRO_GAP) / MACRO_COLS)

void graphics_draw_macro_tab(void)
{
    const ThemeColors *tc = graphics_get_colors();
    C2D_Text textObj;

    graphics_draw_dynamic_text(&textObj, "MACRO DECK", 10, 8, 0.6f, tc->amber);
    graphics_draw_dynamic_text(&textObj, "Tap to execute macro", 10, 28, 0.4f, tc->textDim);

    if (g_macro_count == 0) return;

    int rows      = (g_macro_count + MACRO_COLS - 1) / MACRO_COLS;
    int btn_h     = (MACRO_AREA_H - MACRO_GAP * (rows - 1)) / rows;
    int row_h     = btn_h + MACRO_GAP;
    float lbl_scale = (btn_h < 30) ? 0.35f : 0.42f;
    float lbl_y_off = btn_h * 0.28f;

    for (int i = 0; i < g_macro_count; i++)
    {
        int row = i / MACRO_COLS;
        int col = i % MACRO_COLS;
        int bx  = 10 + col * (MACRO_BTN_W + MACRO_GAP);
        int by  = MACRO_AREA_TOP + row * row_h;

        // Macro button colour from server-supplied colour name → theme slots
        u32 accent = tc->amber;
        if      (strstr(g_macros[i].color, "blue"))   accent = tc->cyan;
        else if (strstr(g_macros[i].color, "red"))    accent = tc->danger;
        else if (strstr(g_macros[i].color, "green"))  accent = tc->cyan;
        else if (strstr(g_macros[i].color, "purple")) accent = tc->amber;

        graphics_draw_hud_panel(bx, by, MACRO_BTN_W, btn_h, tc->panel, accent, 8);
        graphics_draw_dynamic_text(&textObj, g_macros[i].label,
                                   bx + 6, by + lbl_y_off, lbl_scale, tc->text);
    }
}
