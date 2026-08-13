#include "graphics.h"
#include <stdio.h>

extern int g_control_target;
extern int g_volume_level;
extern int g_brightness_level;

void graphics_draw_level_tab(void)
{
    const ThemeColors *tc = graphics_get_colors();
    C2D_Text textObj;
    char textStr[128];

    const int   level_value  = (g_control_target == 0) ? g_volume_level : g_brightness_level;
    float       level        = (float)level_value / 100.0f;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    const char *target_label = (g_control_target == 0) ? "VOLUME" : "BRIGHTNESS";

    graphics_draw_dynamic_text(&textObj, "THROTTLE CONTROL",    34, 20, 0.6f,  tc->amber);
    graphics_draw_dynamic_text(&textObj, target_label,          34, 40, 0.5f,  tc->text);
    graphics_draw_dynamic_text(&textObj, "Drag arc to set level", 34, 58, 0.4f, tc->textDim);

    graphics_draw_hud_panel(22, 86, 220, 146, tc->panel, tc->amber, 16);
    graphics_draw_throttle_dial(132, 146, 70.0f, 36.0f, level, tc->panelDim, tc->amber);

    snprintf(textStr, sizeof(textStr), "%d%%", level_value);
    graphics_draw_dynamic_text(&textObj, textStr, 111, 136, 0.7f, tc->text);
    graphics_draw_dynamic_text(&textObj, "X: Toggle Target", 48, 220, 0.4f, tc->textDim);
}
