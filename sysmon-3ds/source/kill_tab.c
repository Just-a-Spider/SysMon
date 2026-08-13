#include "graphics.h"
#include "network.h"
#include <stdio.h>

extern int g_selected_proc;

void graphics_draw_kill_tab(void)
{
    const ThemeColors *tc = graphics_get_colors();
    C2D_Text textObj;
    char textStr[128];

    graphics_draw_dynamic_text(&textObj, "HANG HUNTER", 10, 8, 0.6f, tc->danger);
    graphics_draw_dynamic_text(&textObj, "Tap process to kill", 10, 28, 0.4f, tc->textDim);

    for (int i = 0; i < g_proc_count; i++)
    {
        int selected = (i == g_selected_proc);
        // Selected fill: dark tint of danger colour
        u32 fill = selected
            ? C2D_Color32((tc->danger & 0xFF) / 5 + 10,
                          ((tc->danger >> 8)  & 0xFF) / 5 + 10,
                          ((tc->danger >> 16) & 0xFF) / 5 + 10, 255)
            : tc->panel;
        u32 accent = selected ? tc->danger : tc->panelDim;
        graphics_draw_hud_panel(10, 50 + (i * 35), 250, 28, fill, accent, 8);
        snprintf(textStr, sizeof(textStr), "[%d] %s (%.1f%%)",
                 (int)g_top_procs[i].pid, g_top_procs[i].name, g_top_procs[i].cpu);
        graphics_draw_dynamic_text(&textObj, textStr,
                                   18, 56 + (i * 35), 0.45f,
                                   selected ? tc->text : tc->textDim);
    }
}
