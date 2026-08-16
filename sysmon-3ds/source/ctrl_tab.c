#include "graphics.h"
#include "network.h"
#include "net_ctrl.h"
#include "net_core.h"
#include <stdio.h>
#include <time.h>
#include <math.h>

// Touch Right Stick definitions for Old 3DS / Touch fallback
#define TOUCH_STICK_CX 220.0f
#define TOUCH_STICK_CY 130.0f
#define TOUCH_STICK_R  55.0f

static void draw_stick_widget(float cx, float cy, float r, s16 sx, s16 sy, const char *label, u32 accent, u32 fill)
{
    C2D_Text textObj;
    // Outer boundary circle
    C2D_DrawCircleSolid(cx, cy, 0, r, fill);
    for (float a = 0; a < 6.28f; a += 0.2f)
    {
        float px = cx + cosf(a) * r;
        float py = cy + sinf(a) * r;
        C2D_DrawRectSolid(px, py, 0, 1.5f, 1.5f, accent);
    }
    // Crosshair axis lines
    C2D_DrawLine(cx - r + 4.0f, cy, accent, cx + r - 4.0f, cy, accent, 1.0f, 0);
    C2D_DrawLine(cx, cy - r + 4.0f, accent, cx, cy + r - 4.0f, accent, 1.0f, 0);

    // Deflected stick center knob
    float def_x = cx + ((float)sx / 32767.0f) * (r - 12.0f);
    float def_y = cy - ((float)sy / 32767.0f) * (r - 12.0f); // Invert Y for screen coords

    C2D_DrawCircleSolid(def_x, def_y, 0, 10.0f, accent);
    C2D_DrawCircleSolid(def_x, def_y, 0, 4.0f, C2D_Color32(20, 25, 30, 255));

    // Label
    graphics_draw_dynamic_text(&textObj, label, cx - 18.0f, cy + r + 6.0f, 0.40f, accent);
}

static void draw_button_pip(float x, float y, float r, const char *label, int pressed, u32 active_clr, u32 idle_clr)
{
    C2D_Text textObj;
    u32 fill = pressed ? active_clr : C2D_Color32(20, 25, 30, 200);
    u32 text_clr = pressed ? C2D_Color32(10, 15, 20, 255) : idle_clr;

    C2D_DrawCircleSolid(x, y, 0, r, fill);
    if (!pressed)
    {
        for (float a = 0; a < 6.28f; a += 0.4f)
        {
            float px = x + cosf(a) * r;
            float py = y + sinf(a) * r;
            C2D_DrawRectSolid(px, py, 0, 1.2f, 1.2f, idle_clr);
        }
    }

    graphics_draw_dynamic_text(&textObj, label, x - 4.5f, y - 6.5f, 0.42f, text_clr);
}

// ---------------------------------------------------------------------------
// Top Screen Gamepad Visualizer & Live Diagnostics HUD
// ---------------------------------------------------------------------------
void graphics_draw_top_screen_ctrl(u32 held, s16 cx, s16 cy, s16 rx, s16 ry)
{
    const ThemeColors *tc = graphics_get_colors();
    C2D_Text textObj;
    char str[128];

    // Background HUD panels
    // Left: Circle Pad + D-Pad
    graphics_draw_hud_panel(10.0f, 10.0f, 185.0f, 180.0f, tc->panelDim, tc->amber, 12.0f);
    // Right: ABXY + Right Stick / C-Stick
    graphics_draw_hud_panel(205.0f, 10.0f, 185.0f, 180.0f, tc->panelDim, tc->cyan, 12.0f);

    // Left Stick (Circle Pad)
    draw_stick_widget(65.0f, 75.0f, 38.0f, cx, cy, "L STICK", tc->amber, tc->panel);

    // D-Pad widget
    const float dpad_cx = 145.0f;
    const float dpad_cy = 75.0f;
    draw_button_pip(dpad_cx, dpad_cy - 18.0f, 8.0f, "U", (held & KEY_DUP) != 0, tc->amber, tc->textDim);
    draw_button_pip(dpad_cx, dpad_cy + 18.0f, 8.0f, "D", (held & KEY_DDOWN) != 0, tc->amber, tc->textDim);
    draw_button_pip(dpad_cx - 18.0f, dpad_cy, 8.0f, "L", (held & KEY_DLEFT) != 0, tc->amber, tc->textDim);
    draw_button_pip(dpad_cx + 18.0f, dpad_cy, 8.0f, "R", (held & KEY_DRIGHT) != 0, tc->amber, tc->textDim);
    graphics_draw_dynamic_text(&textObj, "D-PAD", dpad_cx - 14.0f, dpad_cy + 34.0f, 0.38f, tc->textDim);

    // Right Stick (C-Stick / Touch)
    draw_stick_widget(335.0f, 75.0f, 38.0f, rx, ry, "R STICK", tc->cyan, tc->panel);

    // ABXY Diamond widget
    const float abxy_cx = 255.0f;
    const float abxy_cy = 75.0f;
    draw_button_pip(abxy_cx, abxy_cy - 18.0f, 8.0f, "X", (held & KEY_X) != 0, tc->cyan, tc->textDim);
    draw_button_pip(abxy_cx, abxy_cy + 18.0f, 8.0f, "B", (held & KEY_B) != 0, tc->cyan, tc->textDim);
    draw_button_pip(abxy_cx - 18.0f, abxy_cy, 8.0f, "Y", (held & KEY_Y) != 0, tc->cyan, tc->textDim);
    draw_button_pip(abxy_cx + 18.0f, abxy_cy, 8.0f, "A", (held & KEY_A) != 0, tc->cyan, tc->textDim);
    graphics_draw_dynamic_text(&textObj, "ACTION", abxy_cx - 16.0f, abxy_cy + 34.0f, 0.38f, tc->textDim);

    // Shoulders / Triggers indicator strip
    // L / ZL
    u32 l_clr = (held & KEY_L) ? tc->amber : tc->panel;
    u32 zl_clr = (held & KEY_ZL) ? tc->amber : tc->panel;
    graphics_draw_hud_panel(18.0f, 150.0f, 40.0f, 22.0f, l_clr, tc->amber, 4.0f);
    graphics_draw_dynamic_text(&textObj, "L", 32.0f, 154.0f, 0.42f, (held & KEY_L) ? C2D_Color32(10,15,20,255) : tc->text);
    graphics_draw_hud_panel(64.0f, 150.0f, 40.0f, 22.0f, zl_clr, tc->amber, 4.0f);
    graphics_draw_dynamic_text(&textObj, "ZL", 76.0f, 154.0f, 0.40f, (held & KEY_ZL) ? C2D_Color32(10,15,20,255) : tc->text);

    // R / ZR
    u32 r_clr = (held & KEY_R) ? tc->cyan : tc->panel;
    u32 zr_clr = (held & KEY_ZR) ? tc->cyan : tc->panel;
    graphics_draw_hud_panel(296.0f, 150.0f, 40.0f, 22.0f, zr_clr, tc->cyan, 4.0f);
    graphics_draw_dynamic_text(&textObj, "ZR", 306.0f, 154.0f, 0.40f, (held & KEY_ZR) ? C2D_Color32(10,15,20,255) : tc->text);
    graphics_draw_hud_panel(342.0f, 150.0f, 40.0f, 22.0f, r_clr, tc->cyan, 4.0f);
    graphics_draw_dynamic_text(&textObj, "R", 356.0f, 154.0f, 0.42f, (held & KEY_R) ? C2D_Color32(10,15,20,255) : tc->text);

    // Start / Select
    graphics_draw_hud_panel(112.0f, 150.0f, 38.0f, 22.0f, (held & KEY_SELECT) ? tc->amber : tc->panel, tc->textDim, 4.0f);
    graphics_draw_dynamic_text(&textObj, "SEL", 118.0f, 155.0f, 0.35f, (held & KEY_SELECT) ? C2D_Color32(10,15,20,255) : tc->textDim);
    graphics_draw_hud_panel(154.0f, 150.0f, 38.0f, 22.0f, (held & KEY_START) ? tc->cyan : tc->panel, tc->textDim, 4.0f);
    graphics_draw_dynamic_text(&textObj, "STA", 160.0f, 155.0f, 0.35f, (held & KEY_START) ? C2D_Color32(10,15,20,255) : tc->textDim);

    // Bottom diagnostic status banner
    graphics_draw_hud_panel(10.0f, 198.0f, 380.0f, 34.0f, tc->panel, tc->amber, 8.0f);
    snprintf(str, sizeof(str), "UDP: %s:%d", g_profiles[g_profile_index].ip, g_ctrl_port);
    graphics_draw_dynamic_text(&textObj, str, 20.0f, 206.0f, 0.40f, tc->cyan);

    snprintf(str, sizeof(str), "RATE: %d Hz", g_ctrl_packet_rate > 0 ? g_ctrl_packet_rate : 60);
    graphics_draw_dynamic_text(&textObj, str, 190.0f, 206.0f, 0.40f, tc->amber);

    const char *map_str = g_ctrl_physical_map ? "MAP: PSP POS" : "MAP: NINTENDO";
    graphics_draw_dynamic_text(&textObj, map_str, 290.0f, 206.0f, 0.38f, tc->textDim);
}

// ---------------------------------------------------------------------------
// Bottom Screen Controller HUD & Virtual Touch Right Stick
// ---------------------------------------------------------------------------
void graphics_draw_ctrl_tab(float emergency_exit_progress, int touch_active, float touch_rx, float touch_ry)
{
    const ThemeColors *tc = graphics_get_colors();
    C2D_Text textObj;
    char str[128];

    // 1. Top Header: [ EXIT CONTROLLER ] Button
    graphics_draw_hud_panel(14.0f, 10.0f, 248.0f, 38.0f, tc->panel, tc->danger, 8.0f);
    C2D_DrawCircleSolid(30.0f, 29.0f, 0, 4.0f, tc->danger);
    graphics_draw_dynamic_text(&textObj, "EXIT CONTROLLER", 44.0f, 20.0f, 0.48f, tc->danger);
    graphics_draw_dynamic_text(&textObj, "(TAP TO RETURN)", 165.0f, 22.0f, 0.35f, tc->textDim);

    // 2. Mapping Switch Panel (Left side)
    graphics_draw_hud_panel(14.0f, 56.0f, 110.0f, 65.0f, tc->panel, tc->amber, 8.0f);
    graphics_draw_dynamic_text(&textObj, "BUTTON LAYOUT", 22.0f, 64.0f, 0.38f, tc->textDim);
    if (g_ctrl_physical_map)
    {
        graphics_draw_dynamic_text(&textObj, "PHYSICAL", 22.0f, 80.0f, 0.45f, tc->amber);
        graphics_draw_dynamic_text(&textObj, "(PSP / Cross)", 22.0f, 98.0f, 0.32f, tc->textDim);
    }
    else
    {
        graphics_draw_dynamic_text(&textObj, "NINTENDO", 22.0f, 80.0f, 0.45f, tc->cyan);
        graphics_draw_dynamic_text(&textObj, "(Letters A/B)", 22.0f, 98.0f, 0.32f, tc->textDim);
    }

    // 3. Status Information Panel (Left Lower)
    graphics_draw_hud_panel(14.0f, 130.0f, 110.0f, 85.0f, tc->panel, tc->cyan, 8.0f);
    graphics_draw_dynamic_text(&textObj, "PORT 7339 UDP", 22.0f, 138.0f, 0.38f, tc->cyan);
    graphics_draw_dynamic_text(&textObj, "LOW LATENCY", 22.0f, 154.0f, 0.35f, tc->text);
    graphics_draw_dynamic_text(&textObj, "HOLD L+R+SEL", 22.0f, 174.0f, 0.35f, tc->textDim);
    graphics_draw_dynamic_text(&textObj, "TO FORCE EXIT", 22.0f, 190.0f, 0.32f, tc->textDim);

    // 4. Virtual Right Stick Touch Area (Right side)
    graphics_draw_hud_panel(134.0f, 56.0f, 128.0f, 159.0f, tc->panelDim, tc->cyan, 10.0f);
    graphics_draw_dynamic_text(&textObj, "TOUCH RIGHT STICK", 144.0f, 64.0f, 0.36f, tc->cyan);

    // Virtual Touch Stick circle zone
    C2D_DrawCircleSolid(TOUCH_STICK_CX, TOUCH_STICK_CY, 0, 42.0f, tc->panel);
    for (float a = 0; a < 6.28f; a += 0.3f)
    {
        float px = TOUCH_STICK_CX + cosf(a) * 42.0f;
        float py = TOUCH_STICK_CY + sinf(a) * 42.0f;
        C2D_DrawRectSolid(px, py, 0, 1.5f, 1.5f, tc->cyan);
    }
    C2D_DrawLine(TOUCH_STICK_CX - 36.0f, TOUCH_STICK_CY, tc->cyan, TOUCH_STICK_CX + 36.0f, TOUCH_STICK_CY, tc->cyan, 1.0f, 0);
    C2D_DrawLine(TOUCH_STICK_CX, TOUCH_STICK_CY - 36.0f, tc->cyan, TOUCH_STICK_CX, TOUCH_STICK_CY + 36.0f, tc->cyan, 1.0f, 0);

    float knob_x = TOUCH_STICK_CX;
    float knob_y = TOUCH_STICK_CY;
    if (touch_active)
    {
        knob_x = touch_rx;
        knob_y = touch_ry;
    }
    C2D_DrawCircleSolid(knob_x, knob_y, 0, 12.0f, tc->cyan);
    C2D_DrawCircleSolid(knob_x, knob_y, 0, 4.0f, C2D_Color32(10, 15, 20, 255));

    graphics_draw_dynamic_text(&textObj, "SLIDE FOR CAMERA / AIM", 140.0f, 194.0f, 0.30f, tc->textDim);

    // 5. Emergency Exit Combo Overlay (when holding L+R+SELECT)
    if (emergency_exit_progress > 0.05f)
    {
        graphics_draw_hud_panel(20.0f, 80.0f, 236.0f, 80.0f, C2D_Color32(10, 10, 15, 240), tc->danger, 10.0f);
        graphics_draw_dynamic_text(&textObj, "! EMERGENCY EXIT !", 50.0f, 92.0f, 0.52f, tc->danger);
        snprintf(str, sizeof(str), "HOLDING: %d%%", (int)(emergency_exit_progress * 100.0f));
        graphics_draw_dynamic_text(&textObj, str, 90.0f, 115.0f, 0.40f, tc->text);
        // Progress bar
        C2D_DrawRectSolid(36.0f, 138.0f, 0, 204.0f, 8.0f, tc->panel);
        C2D_DrawRectSolid(36.0f, 138.0f, 0, 204.0f * emergency_exit_progress, 8.0f, tc->danger);
    }
}
