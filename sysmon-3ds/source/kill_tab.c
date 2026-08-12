#include "graphics.h"
#include "network.h"
#include <stdio.h>

extern int g_selected_proc;

void graphics_draw_kill_tab(void)
{
    C2D_Text textObj;
    char textStr[128];

    graphics_draw_dynamic_text(&textObj, "HANG HUNTER", 10, 8, 0.6f, C2D_Color32(220, 60, 40, 255));
    graphics_draw_dynamic_text(&textObj, "Tap process to kill", 10, 28, 0.4f, C2D_Color32(140, 150, 150, 255));

    for (int i = 0; i < g_proc_count; i++)
    {
        int selected = (i == g_selected_proc);
        u32 fill = selected ? C2D_Color32(50, 24, 20, 255) : C2D_Color32(18, 24, 32, 255);
        u32 accent = selected ? C2D_Color32(220, 60, 40, 255) : C2D_Color32(28, 34, 40, 255);
        graphics_draw_hud_panel(10, 50 + (i * 35), 250, 28, fill, accent, 8);
        snprintf(textStr, sizeof(textStr), "[%d] %s (%.1f%%)", (int)g_top_procs[i].pid, g_top_procs[i].name, g_top_procs[i].cpu);
        graphics_draw_dynamic_text(&textObj, textStr, 18, 56 + (i * 35), 0.45f, selected ? C2D_Color32(230, 230, 220, 255) : C2D_Color32(140, 150, 150, 255));
    }
}
