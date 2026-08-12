#include "graphics.h"
#include "network.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

extern int g_screen_mode;
extern int g_tab_scroll;

static C3D_RenderTarget *top;
static C3D_RenderTarget *bottom;
static C2D_TextBuf dynamicBuf;
static C2D_TextBuf staticBuf;
static C2D_Font customFont;

// ---------------------------------------------------------------------
// NO MAN'S SKY inspired palette
// Warm amber = active / primary. Cool cyan = passive / informational.
// Near-black blue background instead of flat gray.
// ---------------------------------------------------------------------
static u32 clrBg;       // screen background
static u32 clrPanel;    // panel fill
static u32 clrPanelDim; // inactive / track fill (segmented bars, inactive tabs)
static u32 clrText;     // primary text (warm off-white, not pure white)
static u32 clrTextDim;  // secondary/help text
static u32 clrAmber;    // primary accent - active state, CPU
static u32 clrCyan;     // secondary accent - passive telemetry, GPU
static u32 clrDanger;   // warnings / kill actions

// Static texts
static C2D_Text txt_cpu, txt_gpu, txt_history;
static C2D_Text txt_pomo_tab, txt_kill_tab, txt_macro_tab, txt_media_tab, txt_set_tab;
static C2D_Text txt_level_tab;

typedef struct
{
    int mode;
    const char *label;
} TabEntry;

static const TabEntry tabEntries[] = {
    {0, "POMO"},
    {1, "KILL"},
    {2, "MACRO"},
    {3, "MEDIA"},
    {4, "SET"},
    {5, "LEVEL"},
};

// Drawer layout: up to TAB_DRAWER_NOSCROLL_MAX tabs get an equal-height
// slot each (no scrolling needed at all). Beyond that, fall back to
// fixed-height rows plus dedicated tap-arrow zones - deliberately NOT a
// drag gesture, since the 3DS bottom screen is resistive and fast swipes
// can drop touch samples mid-gesture. Every touch is a discrete tap.
#define TAB_DRAWER_NOSCROLL_MAX 7
#define TAB_DRAWER_ARROW_ZONE 24.0f
#define TAB_DRAWER_SCROLL_ROW_H 48.0f
#define TAB_DRAWER_SCROLL_VISIBLE_ROWS 4

// Cache for text that rarely changes, to avoid re-parsing every frame
static char last_clock[8] = "";

static void fill_polygon(const float *vx, const float *vy, int n, u32 color);
static void draw_hud_panel(float x, float y, float w, float h, u32 fill, u32 accent, float chamfer);
static void draw_tab(float x, float y, float w, float h, u32 fill, u32 accent, int active);
static void segmented_bar_impl(float x, float y, float w, float h, float fraction, u32 color, int segments);

static void parse_static_text(C2D_Text *textObj, const char *str)
{
    if (customFont)
    {
        C2D_TextFontParse(textObj, customFont, staticBuf, str);
    }
    else
    {
        C2D_TextParse(textObj, staticBuf, str);
    }
    C2D_TextOptimize(textObj);
}

void graphics_init()
{
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    dynamicBuf = C2D_TextBufNew(4096);
    staticBuf = C2D_TextBufNew(4096);
    customFont = C2D_FontLoad("romfs:/minecraft.bcfnt");

    clrBg = C2D_Color32(8, 12, 18, 255);
    clrPanel = C2D_Color32(18, 24, 32, 255);
    clrPanelDim = C2D_Color32(28, 34, 40, 255);
    clrText = C2D_Color32(230, 230, 220, 255);
    clrTextDim = C2D_Color32(140, 150, 150, 255);
    clrAmber = C2D_Color32(255, 160, 40, 255);
    clrCyan = C2D_Color32(60, 220, 220, 255);
    clrDanger = C2D_Color32(220, 60, 40, 255);

    parse_static_text(&txt_cpu, "CPU");
    parse_static_text(&txt_gpu, "GPU");
    parse_static_text(&txt_history, "HISTORY");
    parse_static_text(&txt_pomo_tab, "POMO");
    parse_static_text(&txt_kill_tab, "KILL");
    parse_static_text(&txt_macro_tab, "MACRO");
    parse_static_text(&txt_media_tab, "MEDIA");
    parse_static_text(&txt_set_tab, "SET");
    parse_static_text(&txt_level_tab, "LEVEL");
}

void graphics_exit()
{
    if (customFont)
        C2D_FontFree(customFont);
    C2D_TextBufDelete(dynamicBuf);
    C2D_TextBufDelete(staticBuf);
    C2D_Fini();
    C3D_Fini();
}

static u32 get_temp_color(float temp)
{
    if (temp < 60.0f)
        return clrCyan;
    if (temp < 80.0f)
        return clrAmber;
    return clrDanger;
}

void graphics_draw_dynamic_text(C2D_Text *textObj, const char *str, float x, float y, float scale, u32 color)
{
    if (customFont)
    {
        C2D_TextFontParse(textObj, customFont, dynamicBuf, str);
    }
    else
    {
        C2D_TextParse(textObj, dynamicBuf, str);
    }
    C2D_TextOptimize(textObj);
    C2D_DrawText(textObj, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

void graphics_draw_static_text(C2D_Text *textObj, float x, float y, float scale, u32 color)
{
    C2D_DrawText(textObj, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

void graphics_draw_hud_panel(float x, float y, float w, float h, u32 fill, u32 accent, float chamfer)
{
    draw_hud_panel(x, y, w, h, fill, accent, chamfer);
}

void graphics_draw_segmented_bar(float x, float y, float w, float h, float fraction, u32 color, int segments)
{
    segmented_bar_impl(x, y, w, h, fraction, color, segments);
}

static C2D_Text *tab_text_for_mode(int mode)
{
    switch (mode)
    {
    case 0:
        return &txt_pomo_tab;
    case 1:
        return &txt_kill_tab;
    case 2:
        return &txt_macro_tab;
    case 3:
        return &txt_media_tab;
    case 4:
        return &txt_set_tab;
    case 5:
        return &txt_level_tab;
    default:
        return &txt_pomo_tab;
    }
}

int graphics_tab_count()
{
    return (int)(sizeof(tabEntries) / sizeof(tabEntries[0]));
}

static u32 tab_accent_for_mode(int mode)
{
    switch (mode)
    {
    case 0:
        return clrAmber;
    case 1:
        return clrDanger;
    case 2:
        return clrAmber;
    case 3:
        return clrCyan;
    case 4:
        return clrCyan;
    case 5:
        return clrAmber;
    default:
        return clrAmber;
    }
}

static int clamp_tab_scroll(int scroll)
{
    int max_scroll = graphics_tab_count() - TAB_DRAWER_SCROLL_VISIBLE_ROWS;
    if (max_scroll < 0)
        max_scroll = 0;
    if (scroll < 0)
        return 0;
    if (scroll > max_scroll)
        return max_scroll;
    return scroll;
}

int graphics_tab_uses_scroll(void)
{
    return graphics_tab_count() > TAB_DRAWER_NOSCROLL_MAX;
}

// Unified drawer touch handler. Every touch on the drawer resolves to
// exactly one outcome - no gesture inference:
//   returns >= 0   -> a tab row was tapped; that's the new g_screen_mode
//   returns -2     -> an arrow was tapped; *out_scroll holds the new scroll
//   returns -1     -> touch missed the drawer, or hit a disabled arrow
int graphics_tab_touch_hit(float px, float py, int scroll, int *out_scroll)
{
    if (px < 270.0f || px > 320.0f || py < 0.0f || py > 240.0f)
        return -1;

    int total = graphics_tab_count();

    if (!graphics_tab_uses_scroll())
    {
        float row_h = 240.0f / (float)total;
        int index = (int)(py / row_h);
        if (index < 0 || index >= total)
            return -1;
        return tabEntries[index].mode;
    }

    scroll = clamp_tab_scroll(scroll);
    int max_scroll = total - TAB_DRAWER_SCROLL_VISIBLE_ROWS;
    if (max_scroll < 0)
        max_scroll = 0;

    if (py < TAB_DRAWER_ARROW_ZONE)
    {
        if (scroll > 0 && out_scroll)
        {
            *out_scroll = scroll - 1;
            return -2;
        }
        return -1;
    }

    float rows_bottom = TAB_DRAWER_ARROW_ZONE + TAB_DRAWER_SCROLL_ROW_H * TAB_DRAWER_SCROLL_VISIBLE_ROWS;
    if (py >= rows_bottom)
    {
        if (scroll < max_scroll && out_scroll)
        {
            *out_scroll = scroll + 1;
            return -2;
        }
        return -1;
    }

    int slot = (int)((py - TAB_DRAWER_ARROW_ZONE) / TAB_DRAWER_SCROLL_ROW_H);
    int index = scroll + slot;
    if (slot < 0 || slot >= TAB_DRAWER_SCROLL_VISIBLE_ROWS || index >= total)
        return -1;
    return tabEntries[index].mode;
}

static void draw_scroll_marker(float x, float y, int up, u32 color)
{
    float vx[3];
    float vy[3];
    if (up)
    {
        vx[0] = x + 10.0f;
        vy[0] = y + 6.0f;
        vx[1] = x + 4.0f;
        vy[1] = y + 14.0f;
        vx[2] = x + 16.0f;
        vy[2] = y + 14.0f;
    }
    else
    {
        vx[0] = x + 10.0f;
        vy[0] = y + 14.0f;
        vx[1] = x + 4.0f;
        vy[1] = y + 6.0f;
        vx[2] = x + 16.0f;
        vy[2] = y + 6.0f;
    }
    fill_polygon(vx, vy, 3, color);
}

static void draw_ring_segment(float cx, float cy, float inner_r, float outer_r, float start_angle, float end_angle, u32 color)
{
    float vx[4];
    float vy[4];
    vx[0] = cx + cosf(start_angle) * outer_r;
    vy[0] = cy - sinf(start_angle) * outer_r;
    vx[1] = cx + cosf(end_angle) * outer_r;
    vy[1] = cy - sinf(end_angle) * outer_r;
    vx[2] = cx + cosf(end_angle) * inner_r;
    vy[2] = cy - sinf(end_angle) * inner_r;
    vx[3] = cx + cosf(start_angle) * inner_r;
    vy[3] = cy - sinf(start_angle) * inner_r;
    fill_polygon(vx, vy, 4, color);
}

void graphics_draw_throttle_dial(float cx, float cy, float outer_r, float inner_r, float level, u32 fill, u32 accent)
{
    const float start_angle = 2.3561945f;
    const float sweep = 4.7123890f;
    const int segments = 28;

    if (level < 0.0f)
        level = 0.0f;
    if (level > 1.0f)
        level = 1.0f;

    for (int i = 0; i < segments; i++)
    {
        float t1 = (float)i / (float)segments;
        float t2 = (float)(i + 1) / (float)segments;
        float a1 = start_angle + sweep * t1;
        float a2 = start_angle + sweep * t2;
        u32 seg_color = (t2 <= level) ? accent : fill;
        draw_ring_segment(cx, cy, inner_r, outer_r, a1, a2, seg_color);
    }

    C2D_DrawCircleSolid(cx, cy, 0.0f, inner_r - 7.0f, clrBg);
    C2D_DrawCircleSolid(cx, cy, 0.0f, 3.0f, accent);
}

void graphics_draw_tab_drawer(int active_mode, int scroll)
{
    int total = graphics_tab_count();
    draw_hud_panel(270, 0, 50, 240, clrPanel, clrAmber, 10);

    if (!graphics_tab_uses_scroll())
    {
        // Every tab gets an equal-height slot - nothing to scroll, ever,
        // for as many tabs as comfortably fit.
        float row_h = 240.0f / (float)total;
        float label_scale = (row_h < 40.0f) ? 0.36f : 0.42f;
        for (int i = 0; i < total; i++)
        {
            float y = i * row_h;
            int mode = tabEntries[i].mode;
            int active = (mode == active_mode);
            draw_tab(270, y, 50, row_h, active ? clrPanelDim : clrPanel, tab_accent_for_mode(mode), active);
            graphics_draw_static_text(tab_text_for_mode(mode), 282, y + (row_h * 0.5f) - 6.0f, label_scale, clrText);
        }
        return;
    }

    // Scroll mode: fixed-height rows plus dedicated tap-arrow zones at the
    // top and bottom of the drawer. Deliberately not drag-to-scroll - see
    // the comment on TAB_DRAWER_NOSCROLL_MAX for why.
    scroll = clamp_tab_scroll(scroll);
    int max_scroll = total - TAB_DRAWER_SCROLL_VISIBLE_ROWS;
    if (max_scroll < 0)
        max_scroll = 0;

    u32 up_color = (scroll > 0) ? clrAmber : clrPanelDim;
    u32 down_color = (scroll < max_scroll) ? clrAmber : clrPanelDim;
    float rows_bottom = TAB_DRAWER_ARROW_ZONE + TAB_DRAWER_SCROLL_ROW_H * TAB_DRAWER_SCROLL_VISIBLE_ROWS;

    draw_scroll_marker(277, 2, 1, up_color);
    draw_scroll_marker(277, rows_bottom + 2, 0, down_color);
    C2D_DrawRectSolid(270, TAB_DRAWER_ARROW_ZONE - 1, 0, 50, 1, clrPanelDim);
    C2D_DrawRectSolid(270, rows_bottom, 0, 50, 1, clrPanelDim);

    for (int slot = 0; slot < TAB_DRAWER_SCROLL_VISIBLE_ROWS; slot++)
    {
        int index = scroll + slot;
        if (index >= total)
            break;

        int y = (int)TAB_DRAWER_ARROW_ZONE + slot * (int)TAB_DRAWER_SCROLL_ROW_H;
        int mode = tabEntries[index].mode;
        int active = (mode == active_mode);
        draw_tab(270, y, 50, TAB_DRAWER_SCROLL_ROW_H, active ? clrPanelDim : clrPanel, tab_accent_for_mode(mode), active);
        graphics_draw_static_text(tab_text_for_mode(mode), 282, y + 16, 0.42f, clrText);
    }
}

// ---------------------------------------------------------------------
// NMS-style primitives
// ---------------------------------------------------------------------

// Fills an arbitrary convex polygon by triangle-fanning from its centroid.
static void fill_polygon(const float *vx, const float *vy, int n, u32 color)
{
    float cx = 0.0f, cy = 0.0f;
    for (int i = 0; i < n; i++)
    {
        cx += vx[i];
        cy += vy[i];
    }
    cx /= n;
    cy /= n;
    for (int i = 0; i < n; i++)
    {
        int j = (i + 1) % n;
        C2D_DrawTriangle(cx, cy, color, vx[i], vy[i], color, vx[j], vy[j], color, 0.0f);
    }
}

// A slow, smooth 0..1 pulse driven by system time. Used for glow/breathing effects.
static float pulse01(float speed)
{
    float t = (float)(osGetTime() % 1000000) / 1000.0f;
    return (sinf(t * speed) + 1.0f) * 0.5f;
}

// Core panel shape: rectangle with the top-left and bottom-right corners
// chamfered off, a thin accent line along each cut, and corner brackets
// on the two remaining square corners. This is the single biggest visual
// signature of the reskin - every panel on both screens uses this.
static void draw_hud_panel(float x, float y, float w, float h, u32 fill, u32 accent, float chamfer)
{
    float vx[6] = {x + chamfer, x + w, x + w, x + w - chamfer, x, x};
    float vy[6] = {y, y, y + h - chamfer, y + h, y + h, y + chamfer};
    fill_polygon(vx, vy, 6, fill);

    // accent line along each chamfer cut
    C2D_DrawLine(vx[5], vy[5], accent, vx[0], vy[0], accent, 1.5f, 0);
    C2D_DrawLine(vx[2], vy[2], accent, vx[3], vy[3], accent, 1.5f, 0);

    // corner brackets on the square (non-chamfered) corners
    float bl = (w < 60.0f || h < 60.0f) ? 6.0f : 10.0f;
    C2D_DrawLine(x + w - bl, y, accent, x + w, y, accent, 1.5f, 0);
    C2D_DrawLine(x + w, y, accent, x + w, y + bl, accent, 1.5f, 0);
    C2D_DrawLine(x, y + h - bl, accent, x, y + h, accent, 1.5f, 0);
    C2D_DrawLine(x, y + h, accent, x + bl, y + h, accent, 1.5f, 0);
}

// Right-pointing arrow tab used for the bottom-screen mode selector.
static void draw_tab(float x, float y, float w, float h, u32 fill, u32 accent, int active)
{
    float c = h * 0.28f;
    float vx[5] = {x + c, x + w, x + w, x + c, x};
    float vy[5] = {y, y, y + h, y + h, y + h / 2.0f};
    fill_polygon(vx, vy, 5, fill);
    if (active)
    {
        C2D_DrawLine(vx[4], vy[4], accent, vx[0], vy[0], accent, 2.0f, 0);
        C2D_DrawLine(vx[4], vy[4], accent, vx[3], vy[3], accent, 2.0f, 0);
        // pointer nudging into the content area
        float px[3] = {x - 6.0f, x, x};
        float py[3] = {y + h / 2.0f, y + h / 2.0f - 5.0f, y + h / 2.0f + 5.0f};
        fill_polygon(px, py, 3, accent);
    }
}

// Segmented bar: reads as "reactor throughput" rather than a plain fill.
static void segmented_bar_impl(float x, float y, float w, float h, float fraction, u32 color, int segments)
{
    if (fraction < 0.0f)
        fraction = 0.0f;
    if (fraction > 1.0f)
        fraction = 1.0f;
    float gap = 2.0f;
    float seg_w = (w - gap * (segments - 1)) / segments;
    int filled = (int)(fraction * segments + 0.5f);
    for (int i = 0; i < segments; i++)
    {
        float sx = x + i * (seg_w + gap);
        u32 c = (i < filled) ? color : clrPanelDim;
        C2D_DrawRectSolid(sx, y, 0, seg_w, h, c);
    }
}

// Sparse horizontal scanlines over a region - cheap "screen glow" texture.
static void draw_scanlines(float x, float y, float w, float h, u8 alpha)
{
    for (float ly = y; ly < y + h; ly += 4.0f)
    {
        C2D_DrawRectSolid(x, ly, 0, w, 1, C2D_Color32(255, 255, 255, alpha));
    }
}

void graphics_draw_frame(const char *ip_buffer, int port)
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    float glow = pulse01(2.0f);              // 0..1 breathing value, shared this frame
    u8 bracketAlpha = (u8)(160 + glow * 95); // corner-bracket "alive" pulse
    u32 accentPulse = C2D_Color32(255, 160, 40, bracketAlpha);

    // ============================= TOP SCREEN =============================
    C2D_TargetClear(top, clrBg);
    C2D_SceneBegin(top);
    C2D_TextBufClear(dynamicBuf);

    char textStr[256];
    C2D_Text textObj;

    // Danger state: pulsing border wash instead of a flat full-screen flash
    if (g_cpu_temp > 85.0f)
    {
        u8 a = (u8)(40 + glow * 60);
        C2D_DrawRectSolid(0, 0, 0, 400, 6, C2D_Color32(220, 60, 40, a));
        C2D_DrawRectSolid(0, 234, 0, 400, 6, C2D_Color32(220, 60, 40, a));
    }

    draw_scanlines(0, 0, 400, 240, 4);

    // Panels
    draw_hud_panel(10, 10, 185, 90, clrPanel, clrAmber, 14);     // CPU
    draw_hud_panel(205, 10, 185, 90, clrPanel, clrCyan, 14);     // GPU
    draw_hud_panel(10, 110, 250, 90, clrPanel, accentPulse, 14); // History
    draw_hud_panel(270, 110, 120, 90, clrPanel, clrAmber, 12);   // Time/Weather

    graphics_draw_static_text(&txt_cpu, 24, 18, 0.55f, clrTextDim);
    graphics_draw_static_text(&txt_gpu, 219, 18, 0.55f, clrTextDim);
    graphics_draw_static_text(&txt_history, 24, 118, 0.55f, clrTextDim);

    // CPU
    snprintf(textStr, sizeof(textStr), "%.1f%%", g_cpu_usage);
    graphics_draw_dynamic_text(&textObj, textStr, 24, 38, 0.6f, clrText);
    graphics_draw_segmented_bar(24, 60, 155, 8, g_cpu_usage / 100.0f, clrAmber, 20);

    snprintf(textStr, sizeof(textStr), "%.1fC  FAN %d", g_cpu_temp, g_cpu_fan);
    graphics_draw_dynamic_text(&textObj, textStr, 24, 75, 0.42f, clrTextDim);
    graphics_draw_segmented_bar(24, 88, 155, 6, g_cpu_temp / 100.0f, get_temp_color(g_cpu_temp), 20);

    // GPU
    snprintf(textStr, sizeof(textStr), "%.1fC  FAN %d", g_gpu_temp, g_gpu_fan);
    graphics_draw_dynamic_text(&textObj, textStr, 219, 38, 0.45f, clrText);
    graphics_draw_segmented_bar(219, 58, 155, 8, g_gpu_temp / 100.0f, get_temp_color(g_gpu_temp), 20);

    snprintf(textStr, sizeof(textStr), "FREE RAM  %.2f GB", g_free_ram);
    graphics_draw_dynamic_text(&textObj, textStr, 219, 75, 0.42f, clrTextDim);

    // History graph - scaled to the actual visible min/max, not a fixed 0-100 range
    if (g_history_count > 1)
    {
        float lo = 1000.0f, hi = -1000.0f;
        for (int i = 0; i < g_history_count; i++)
        {
            if (g_temp_history[i] < lo)
                lo = g_temp_history[i];
            if (g_temp_history[i] > hi)
                hi = g_temp_history[i];
            if (g_gpu_temp_history[i] < lo)
                lo = g_gpu_temp_history[i];
            if (g_gpu_temp_history[i] > hi)
                hi = g_gpu_temp_history[i];
        }
        float span = (hi - lo < 5.0f) ? 5.0f : (hi - lo);
        float start_x = 24, start_y = 185, gh = 45.0f;
        float dx = 230.0f / 9.0f;
        for (int i = 0; i < g_history_count - 1; i++)
        {
            float x1 = start_x + (i * dx);
            float x2 = start_x + ((i + 1) * dx);
            float y1 = start_y - ((g_temp_history[i] - lo) / span * gh);
            float y2 = start_y - ((g_temp_history[i + 1] - lo) / span * gh);
            C2D_DrawLine(x1, y1, clrAmber, x2, y2, clrAmber, 2.0f, 0);
            float gy1 = start_y - ((g_gpu_temp_history[i] - lo) / span * gh);
            float gy2 = start_y - ((g_gpu_temp_history[i + 1] - lo) / span * gh);
            C2D_DrawLine(x1, gy1, clrCyan, x2, gy2, clrCyan, 2.0f, 0);
        }
    }

    // Clock / weather - only re-parsed when the value actually changes
    time_t t = time(NULL);
    struct tm *tmv = localtime(&t);
    char clock_str[8];
    snprintf(clock_str, sizeof(clock_str), "%02d:%02d", tmv->tm_hour, tmv->tm_min);
    if (strcmp(last_clock, clock_str) != 0)
    {
        snprintf(last_clock, sizeof(last_clock), "%s", clock_str);
    }
    graphics_draw_dynamic_text(&textObj, last_clock, 282, 128, 1.15f, clrText);
    graphics_draw_dynamic_text(&textObj, g_weather, 282, 168, 0.45f, clrTextDim);

    // Status strip
    u32 statusColor = clrTextDim;
    const char *statusText = "TCP: ERROR";
    if (g_http_status == 1)
    {
        statusColor = clrCyan;
        statusText = "TCP: CONNECTED";
    }
    else if (g_http_status == 0)
    {
        statusColor = clrAmber;
        statusText = "TCP: CONNECTING";
    }
    else
    {
        statusColor = clrDanger;
    }
    graphics_draw_dynamic_text(&textObj, statusText, 12, 208, 0.42f, statusColor);

    if (!g_fetching_enabled)
    {
        graphics_draw_dynamic_text(&textObj, "(PAUSED)", 145, 208, 0.42f, clrAmber);
    }

    u8 battery_level = 0;
    u8 is_charging = 0;
    PTMU_GetBatteryLevel(&battery_level);
    PTMU_GetBatteryChargeState(&is_charging);
    if (is_charging)
    {
        snprintf(textStr, sizeof(textStr), "CHG %d%%", battery_level * 20);
        graphics_draw_dynamic_text(&textObj, textStr, 332, 208, 0.4f, clrCyan);
    }
    else
    {
        snprintf(textStr, sizeof(textStr), "BAT %d%%", battery_level * 20);
        graphics_draw_dynamic_text(&textObj, textStr, 332, 208, 0.4f, battery_level <= 1 ? clrDanger : clrAmber);
    }

    if (g_has_notification)
    {
        draw_hud_panel(0, 0, 400, 26, C2D_Color32(30, 20, 10, 235), clrAmber, 10);
        graphics_draw_dynamic_text(&textObj, "DESKTOP NOTIFICATION", 90, 6, 0.6f, clrAmber);
    }

    // ============================ BOTTOM SCREEN ============================
    C2D_TargetClear(bottom, clrBg);
    C2D_SceneBegin(bottom);
    draw_scanlines(0, 0, 320, 240, 3);

    // Drawer drawn after content to overlay correctly

    if (g_kill_confirm_pid > 0)
    {
        draw_hud_panel(20, 60, 230, 120, C2D_Color32(15, 15, 18, 245), clrDanger, 16);
        snprintf(textStr, sizeof(textStr), "KILL %s?", g_kill_confirm_name);
        graphics_draw_dynamic_text(&textObj, textStr, 34, 82, 0.6f, clrDanger);
        graphics_draw_dynamic_text(&textObj, "(A) CONFIRM", 54, 122, 0.5f, clrAmber);
        graphics_draw_dynamic_text(&textObj, "(B) CANCEL", 54, 142, 0.5f, clrTextDim);
    }
    else if (g_screen_mode == 0)
    {
        graphics_draw_pomo_tab(glow);
    }
    else if (g_screen_mode == 1)
    {
        graphics_draw_kill_tab();
    }
    else if (g_screen_mode == 2)
    {
        graphics_draw_macro_tab();
    }
    else if (g_screen_mode == 3)
    {
        graphics_draw_media_tab();
    }
    else if (g_screen_mode == 4)
    {
        graphics_draw_settings_tab(ip_buffer, port);
    }
    else if (g_screen_mode == 5)
    {
        graphics_draw_level_tab();
    }

    // Tab drawer
    graphics_draw_tab_drawer(g_screen_mode, g_tab_scroll);

    C3D_FrameEnd(0);
}