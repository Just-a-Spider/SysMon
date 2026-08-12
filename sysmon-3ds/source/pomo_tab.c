#include "graphics.h"
#include <stdio.h>

extern int g_pomodoro_seconds;
extern int g_pomodoro_active;
extern int g_pomodoro_preset_index;
extern int g_pomodoro_custom_minutes;

static const char *preset_label(int index)
{
    switch (index)
    {
    case 0:
        return "15 MIN";
    case 1:
        return "25 MIN";
    case 2:
        return "45 MIN";
    case 3:
        return "60 MIN";
    default:
        return "CUSTOM";
    }
}

void graphics_draw_pomo_tab(float glow)
{
    C2D_Text textObj;
    char textStr[128];

    graphics_draw_dynamic_text(&textObj, "POMODORO TIMER", 40, 30, 0.65f, C2D_Color32(255, 160, 40, 255));
    snprintf(textStr, sizeof(textStr), "%02d:%02d", g_pomodoro_seconds / 60, g_pomodoro_seconds % 60);
    graphics_draw_dynamic_text(&textObj, textStr, 78, 70, 1.5f, C2D_Color32(230, 230, 220, 255));

    if (g_pomodoro_active)
    {
        u8 a = (u8)(120 + glow * 100);
        C2D_DrawRectSolid(60, 155, 0, 200, 3, C2D_Color32(255, 160, 40, a));
    }

    graphics_draw_hud_panel(22, 120, 220, 42, C2D_Color32(18, 24, 32, 255), C2D_Color32(255, 160, 40, 255), 10);
    graphics_draw_dynamic_text(&textObj, "A: Start/Pause", 34, 132, 0.42f, C2D_Color32(140, 150, 150, 255));
    graphics_draw_dynamic_text(&textObj, "Y: Reset", 34, 146, 0.42f, C2D_Color32(140, 150, 150, 255));

    graphics_draw_hud_panel(22, 168, 220, 56, C2D_Color32(18, 24, 32, 255), C2D_Color32(60, 220, 220, 255), 10);
    snprintf(textStr, sizeof(textStr), "PRESET  %s", preset_label(g_pomodoro_preset_index));
    graphics_draw_dynamic_text(&textObj, textStr, 34, 180, 0.42f, C2D_Color32(230, 230, 220, 255));
    if (g_pomodoro_preset_index == 4)
    {
        snprintf(textStr, sizeof(textStr), "CUSTOM  %d MIN", g_pomodoro_custom_minutes);
        graphics_draw_dynamic_text(&textObj, textStr, 34, 192, 0.42f, C2D_Color32(140, 150, 150, 255));
    }
    else
    {
        graphics_draw_dynamic_text(&textObj, "L/R: Change Preset", 34, 192, 0.42f, C2D_Color32(140, 150, 150, 255));
    }

    graphics_draw_dynamic_text(&textObj, "D-UP/DOWN: Custom +/-", 45, 216, 0.36f, C2D_Color32(140, 150, 150, 255));
}
