// settings_tab.c — SET tab: main view + Theme Manager + Server Manager +
//                          Theme Editor + Server Editor sub-views
//
// Navigation model (handled in main.c, drawn here):
//   D-UP/DOWN  : move between THEME row (0) and SERVER row (1)
//   D-LEFT/RIGHT : rotate options within focused row (live preview)
//   A          : apply current preview for focused row
//   Y          : open editor (sub=2) for current selection
//   X          : open manager (sub=1) for focused category
//   B          : back to main SET

#include "graphics.h"
#include "network.h"
#include "input.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Swatch palette — used in the theme editor
// ---------------------------------------------------------------------------
typedef struct { const char *name; u8 r, g, b; } SwatchColor;

static const SwatchColor s_pal[] = {
    {"AMBER",  255, 160,  40},
    {"RED",    220,  60,  40},
    {"CYAN",    60, 220, 220},
    {"GREEN",   60, 200,  80},
    {"ORANGE", 255, 120,  20},
    {"WHITE",  200, 220, 240},
    {"PURPLE", 180,  60, 220},
    {"PINK",   220,  80, 140},
    {"DKBG",     8,  12,  18},
    {"MAROON",  14,   6,   6},
    {"NAVY",     6,   9,  16},
    {"SLATE",   18,  24,  32},
};
#define PAL_COUNT 12
// Palette field names (BG, PANEL, AC1, AC2, DNG = indices 0..4)
static const char *s_field_names[] = {"BG", "PANEL", "AC1", "AC2", "DNG"};
#define EDITOR_FIELDS 5

// ---------------------------------------------------------------------------
// Helper: draw a row label + filled swatch rect
// ---------------------------------------------------------------------------
static void draw_swatch_row(C2D_Text *t, const char *label,
                            float rx, float ry, float rw, float rh,
                            u32 color, u32 text_color)
{
    graphics_draw_dynamic_text(t, label, rx, ry + 3, 0.38f, text_color);
    C2D_DrawRectSolid(rx + 42, ry, 0, rw, rh, color);
}

// ---------------------------------------------------------------------------
// SET MAIN VIEW  (set_sub == 0)
// ---------------------------------------------------------------------------
void graphics_draw_settings_tab(int set_row, int preview_idx)
{
    const ThemeColors *tc = graphics_get_colors();
    C2D_Text textObj;
    char textStr[128];

    // ---- Setting rows ----
    // Row 0: THEME
    {
        int focused  = (set_row == 0);
        u32 acc      = focused ? tc->amber  : tc->panelDim;
        u32 txt_col  = focused ? tc->amber  : tc->textDim;
        graphics_draw_hud_panel(10, 8, 250, 34, tc->panel, acc, 8);
        if (focused)
            graphics_draw_dynamic_text(&textObj, ">", 14, 18, 0.45f, tc->amber);
        graphics_draw_dynamic_text(&textObj, "THEME", 28, 18, 0.45f, txt_col);
        // Centred option name
        int tidx = (set_row == 0) ? preview_idx : g_theme_index;
        snprintf(textStr, sizeof(textStr), "< %s >",
                 graphics_theme_name(tidx));
        graphics_draw_dynamic_text(&textObj, textStr, 95, 18, 0.45f, txt_col);
        // Mini colour swatch
        if (tidx >= 0)
            graphics_draw_theme_preview(205, 12, 50, 26, tidx);
    }

    // Row 1: SERVER
    {
        int focused  = (set_row == 1);
        u32 acc      = focused ? tc->cyan   : tc->panelDim;
        u32 txt_col  = focused ? tc->cyan   : tc->textDim;
        int pidx     = focused ? preview_idx : g_profile_index;
        const char *pname = (pidx >= 0 && pidx < g_profile_count)
                            ? g_profiles[pidx].name : "---";
        graphics_draw_hud_panel(10, 46, 250, 34, tc->panel, acc, 8);
        if (focused)
            graphics_draw_dynamic_text(&textObj, ">", 14, 56, 0.45f, tc->cyan);
        graphics_draw_dynamic_text(&textObj, "SERVER", 28, 56, 0.45f, txt_col);
        snprintf(textStr, sizeof(textStr), "< %s >", pname);
        graphics_draw_dynamic_text(&textObj, textStr, 95, 56, 0.45f, txt_col);
    }

    // Divider
    C2D_DrawRectSolid(10, 84, 0, 250, 1, tc->panelDim);

    // ---- Info panel ----
    if (set_row == 0)
    {
        // Theme colour swatches (5 rows: BG, PANEL, AC1, AC2, DNG)
        ThemePreset *t = graphics_get_theme_mut(preview_idx);
        if (t)
        {
            float ry = 92.0f, rh = 16.0f, rw = 80.0f;
            u32 cols[5] = {
                C2D_Color32(t->bg_r,  t->bg_g,  t->bg_b,  255),
                C2D_Color32(t->pan_r, t->pan_g, t->pan_b, 255),
                C2D_Color32(t->ac1_r, t->ac1_g, t->ac1_b, 255),
                C2D_Color32(t->ac2_r, t->ac2_g, t->ac2_b, 255),
                C2D_Color32(t->dng_r, t->dng_g, t->dng_b, 255),
            };
            for (int i = 0; i < 5; i++)
            {
                draw_swatch_row(&textObj, s_field_names[i],
                                16, ry + i * (rh + 5), rw, rh,
                                cols[i], tc->textDim);
            }
        }
    }
    else
    {
        // Server connection info
        int pidx = (preview_idx >= 0 && preview_idx < g_profile_count)
                   ? preview_idx : g_profile_index;
        if (pidx >= 0 && pidx < g_profile_count)
        {
            const ServerProfile *p = &g_profiles[pidx];
            snprintf(textStr, sizeof(textStr), "IP      %s", p->ip);
            graphics_draw_dynamic_text(&textObj, textStr, 16, 95, 0.42f, tc->text);
            snprintf(textStr, sizeof(textStr), "PORT    %d", p->port);
            graphics_draw_dynamic_text(&textObj, textStr, 16, 113, 0.42f, tc->text);
            graphics_draw_dynamic_text(&textObj, "PIN     ****", 16, 131, 0.42f, tc->textDim);

            const char *st; u32 sc;
            if      (g_http_status == 1) { st="STATUS  CONNECTED";  sc=tc->cyan;   }
            else if (g_http_status == 0) { st="STATUS  CONNECTING"; sc=tc->amber;  }
            else                         { st="STATUS  ERROR";       sc=tc->danger; }
            graphics_draw_dynamic_text(&textObj, st, 16, 149, 0.42f, sc);
        }
    }

    // ---- Hint bar ----
    C2D_DrawRectSolid(10, 206, 0, 250, 1, tc->panelDim);
    graphics_draw_dynamic_text(&textObj, "A:APPLY  Y:EDIT  X:MANAGE", 16, 212, 0.38f, tc->textDim);
}

// ---------------------------------------------------------------------------
// MANAGER SUB-VIEW  (set_sub == 1)
// ---------------------------------------------------------------------------
void graphics_draw_settings_manager(int set_row, int preview_idx)
{
    const ThemeColors *tc = graphics_get_colors();
    C2D_Text textObj;
    char textStr[64];

    u32 hdr_acc = (set_row == 0) ? tc->amber : tc->cyan;
    const char *title = (set_row == 0) ? "THEME MANAGER" : "SERVER MANAGER";

    graphics_draw_hud_panel(0, 0, 260, 20, tc->panel, hdr_acc, 0);
    graphics_draw_dynamic_text(&textObj, title, 8, 3, 0.45f, hdr_acc);
    graphics_draw_dynamic_text(&textObj, "B:BACK", 196, 3, 0.38f, tc->textDim);

    int count = (set_row == 0) ? graphics_theme_count() : g_profile_count;

    for (int i = 0; i < count && i < 4; i++)
    {
        float ry  = 24.0f + i * 46.0f;
        int active = (i == ((set_row == 0) ? g_theme_index : g_profile_index));
        int sel    = (i == preview_idx);

        u32 acc  = sel    ? hdr_acc    : (active ? tc->amber : tc->panelDim);
        u32 fill = active ? tc->panel  : tc->panelDim;

        graphics_draw_hud_panel(8, ry, 200, 38, fill, acc, 6);

        if (set_row == 0)
        {
            // Theme row: name + mini preview
            graphics_draw_dynamic_text(&textObj,
                                       active ? "*" : " ", 14, ry + 10, 0.5f, tc->amber);
            graphics_draw_dynamic_text(&textObj, graphics_theme_name(i),
                                       26, ry + 10, 0.45f, sel ? hdr_acc : tc->text);
            graphics_draw_theme_preview(150, ry + 5, 52, 28, i);
        }
        else
        {
            // Profile row: name + IP
            graphics_draw_dynamic_text(&textObj,
                                       active ? "*" : " ", 14, ry + 5,  0.5f,  tc->cyan);
            graphics_draw_dynamic_text(&textObj, g_profiles[i].name,
                                       26, ry + 5,  0.45f, sel ? hdr_acc : tc->text);
            snprintf(textStr, sizeof(textStr), "%s:%d", g_profiles[i].ip, g_profiles[i].port);
            graphics_draw_dynamic_text(&textObj, textStr,
                                       26, ry + 20, 0.35f, tc->textDim);
        }

        // DEL zone (if more than one exists)
        if (count > 1)
        {
            graphics_draw_hud_panel(212, ry + 6, 38, 26, tc->panel, tc->danger, 4);
            graphics_draw_dynamic_text(&textObj, "DEL", 218, ry + 12, 0.38f, tc->danger);
        }
    }

    // [+ NEW] button
    if (count < MAX_THEMES)
    {
        float ny = 24.0f + 4 * 46.0f;
        if (ny > 200.0f) ny = 200.0f;
        graphics_draw_hud_panel(8, ny, 244, 30, tc->panel, hdr_acc, 6);
        graphics_draw_dynamic_text(&textObj, "+ NEW", 100, ny + 8, 0.45f, hdr_acc);
    }
}

// ---------------------------------------------------------------------------
// EDITOR SUB-VIEW  (set_sub == 2)
// ---------------------------------------------------------------------------
void graphics_draw_settings_editor(int set_row, int editing,
                                   int edit_field, int edit_swatch)
{
    const ThemeColors *tc = graphics_get_colors();
    C2D_Text textObj;
    char textStr[64];

    u32 hdr_acc = (set_row == 0) ? tc->amber : tc->cyan;

    if (set_row == 0)
    {
        // ---- THEME EDITOR ----
        ThemePreset *t = graphics_get_theme_mut(editing);
        if (!t) return;

        snprintf(textStr, sizeof(textStr), "EDIT: %s", t->name);
        graphics_draw_hud_panel(0, 0, 260, 20, tc->panel, hdr_acc, 0);
        graphics_draw_dynamic_text(&textObj, textStr, 8, 3, 0.45f, hdr_acc);
        graphics_draw_dynamic_text(&textObj, "B:CANCEL", 186, 3, 0.38f, tc->textDim);

        // 5 colour field rows
        u8 *field_ptrs[EDITOR_FIELDS][3] = {
            {&t->bg_r,  &t->bg_g,  &t->bg_b},
            {&t->pan_r, &t->pan_g, &t->pan_b},
            {&t->ac1_r, &t->ac1_g, &t->ac1_b},
            {&t->ac2_r, &t->ac2_g, &t->ac2_b},
            {&t->dng_r, &t->dng_g, &t->dng_b},
        };

        // Swatch scroll: show 6 at a time, centred on edit_swatch
        int swatch_offset = edit_swatch - 2;
        if (swatch_offset < 0)             swatch_offset = 0;
        if (swatch_offset > PAL_COUNT - 6) swatch_offset = PAL_COUNT - 6;

        for (int fi = 0; fi < EDITOR_FIELDS; fi++)
        {
            float ry   = 24.0f + fi * 35.0f;
            int focused = (fi == edit_field);
            u32 lbl_col = focused ? hdr_acc : tc->textDim;

            // Current colour swatch
            u32 cur_col = C2D_Color32(*field_ptrs[fi][0],
                                      *field_ptrs[fi][1],
                                      *field_ptrs[fi][2], 255);
            graphics_draw_dynamic_text(&textObj, s_field_names[fi],
                                       8, ry + 6, 0.38f, lbl_col);
            C2D_DrawRectSolid(48, ry + 2, 0, 24, 24, cur_col);
            if (focused)
                C2D_DrawRectSolid(46, ry, 0, 28, 28,
                                  C2D_Color32(hdr_acc & 0xFF,
                                              (hdr_acc >> 8)  & 0xFF,
                                              (hdr_acc >> 16) & 0xFF, 80));

            // 6 palette swatches for this row
            for (int si = 0; si < 6; si++)
            {
                int pi = swatch_offset + si;
                if (pi >= PAL_COUNT) break;
                float sx  = 80.0f + si * 30.0f;
                u32   sc  = C2D_Color32(s_pal[pi].r, s_pal[pi].g, s_pal[pi].b, 255);
                C2D_DrawRectSolid(sx, ry + 4, 0, 24, 22, sc);
                // Highlight selected swatch in focused row
                if (focused && pi == edit_swatch)
                    C2D_DrawRectSolid(sx - 2, ry + 2, 0, 28, 26,
                                      C2D_Color32(hdr_acc & 0xFF,
                                                  (hdr_acc >> 8)  & 0xFF,
                                                  (hdr_acc >> 16) & 0xFF, 160));
            }
        }

        // Name field
        C2D_DrawRectSolid(8, 202, 0, 244, 1, tc->panelDim);
        snprintf(textStr, sizeof(textStr), "NAME: %s  (tap)", t->name);
        graphics_draw_dynamic_text(&textObj, textStr, 8, 207, 0.38f, tc->textDim);

        // Save hint
        graphics_draw_dynamic_text(&textObj, "A:SWATCH  Y:SAVE  B:CANCEL", 34, 222, 0.38f, tc->textDim);
    }
    else
    {
        // ---- SERVER EDITOR ----
        if (editing < 0 || editing >= g_profile_count) return;
        ServerProfile *p = &g_profiles[editing];

        snprintf(textStr, sizeof(textStr), "EDIT: %s", p->name);
        graphics_draw_hud_panel(0, 0, 260, 20, tc->panel, hdr_acc, 0);
        graphics_draw_dynamic_text(&textObj, textStr, 8, 3, 0.45f, hdr_acc);
        graphics_draw_dynamic_text(&textObj, "B:CANCEL", 186, 3, 0.38f, tc->textDim);

        const char *labels[] = {"NAME", "IP", "PORT", "PIN"};
        char values[4][64];
        snprintf(values[0], 64, "%s", p->name);
        snprintf(values[1], 64, "%s", p->ip);
        snprintf(values[2], 64, "%d", p->port);
        snprintf(values[3], 64, "****");

        for (int fi = 0; fi < 4; fi++)
        {
            float ry    = 28.0f + fi * 44.0f;
            int focused = (fi == edit_field);
            u32 acc     = focused ? hdr_acc : tc->panelDim;
            graphics_draw_hud_panel(8, ry, 244, 36, tc->panel, acc, 6);
            graphics_draw_dynamic_text(&textObj, labels[fi], 14, ry + 6,  0.38f, tc->textDim);
            graphics_draw_dynamic_text(&textObj, values[fi], 70, ry + 10, 0.45f,
                                       focused ? hdr_acc : tc->text);
            if (focused)
                graphics_draw_dynamic_text(&textObj, "TAP", 218, ry + 10, 0.35f, hdr_acc);
        }

        graphics_draw_dynamic_text(&textObj, "A/TAP:EDIT  Y:SAVE  B:CANCEL", 34, 212, 0.38f, tc->textDim);
    }
}
