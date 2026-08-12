#include "graphics.h"
#include <stdio.h>

void graphics_draw_settings_tab(const char *ip_buffer, int port)
{
    C2D_Text textObj;
    char textStr[128];

    graphics_draw_hud_panel(35, 78, 200, 40, C2D_Color32(18, 24, 32, 255), C2D_Color32(255, 160, 40, 255), 12);
    graphics_draw_dynamic_text(&textObj, "Change IP / Port", 65, 88, 0.55f, C2D_Color32(230, 230, 220, 255));
    snprintf(textStr, sizeof(textStr), "SERVER  %s:%d", ip_buffer, port);
    graphics_draw_dynamic_text(&textObj, textStr, 35, 132, 0.45f, C2D_Color32(140, 150, 150, 255));
}
