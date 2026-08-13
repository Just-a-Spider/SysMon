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

// ---------------------------------------------------------------------------
// Theme preset storage (mutable — loaded from / saved to sysmon_theme.txt)
// ---------------------------------------------------------------------------
static ThemePreset s_themes[MAX_THEMES] = {
    // NMS — default amber/cyan on near-black
    { "NMS",
       8, 12, 18,   18, 24, 32,   28, 34, 40,
      255,160, 40,   60,220,220,  220, 60, 40 },
    // BLOOD — red/orange on deep maroon
    { "BLOOD",
      14,  6,  6,   24, 12, 12,   38, 18, 18,
      220, 60, 40,  255,120, 20,  255, 60, 60 },
    // ICE — cyan/white on deep blue
    { "ICE",
       6,  9, 16,   14, 20, 34,   22, 30, 48,
       60,200,255,  200,220,240,  180, 60, 40 },
};
static int s_theme_count = 3;

int g_theme_index = 0;

// ---------------------------------------------------------------------------
// Swatch palette (shared with settings_tab.c via graphics_palette_color)
// ---------------------------------------------------------------------------
typedef struct { u8 r, g, b; } PalEntry;
static const PalEntry s_pal[] = {
    {255,160, 40}, {220, 60, 40}, { 60,220,220}, { 60,200, 80},
    {255,120, 20}, {200,220,240}, {180, 60,220}, {220, 80,140},
    {  8, 12, 18}, { 14,  6,  6}, {  6,  9, 16}, { 18, 24, 32},
};
#define PAL_COUNT_INT 12

void graphics_palette_color(int idx, u8 *r, u8 *g, u8 *b)
{
    if (idx < 0 || idx >= PAL_COUNT_INT) { *r=*g=*b=0; return; }
    *r = s_pal[idx].r; *g = s_pal[idx].g; *b = s_pal[idx].b;
}
int graphics_palette_count(void) { return PAL_COUNT_INT; }

// Live palette — filled by graphics_apply_theme()
static ThemeColors s_colors;

// Runtime clr* values (used internally by all draw functions)
static u32 clrBg;
static u32 clrPanel;
static u32 clrPanelDim;
static u32 clrText;
static u32 clrTextDim;
static u32 clrAmber;
static u32 clrCyan;
static u32 clrDanger;

// Static texts
static C2D_Text txt_cpu, txt_gpu, txt_history;
static C2D_Text txt_pomo_tab, txt_kill_tab, txt_macro_tab, txt_media_tab, txt_set_tab;
static C2D_Text txt_level_tab;

typedef struct { int mode; const char *label; } TabEntry;
static const TabEntry tabEntries[] = {
    {0, "POMO"}, {1, "KILL"}, {2, "MACRO"},
    {3, "MEDIA"},{4, "SET"}, {5, "LEVEL"},
};

#define TAB_DRAWER_NOSCROLL_MAX    7
#define TAB_DRAWER_ARROW_ZONE      24.0f
#define TAB_DRAWER_SCROLL_ROW_H    48.0f
#define TAB_DRAWER_SCROLL_VISIBLE_ROWS 4

static char last_clock[8] = "";

// Forward declarations of static primitives
static void fill_polygon(const float *vx, const float *vy, int n, u32 color);
static void draw_hud_panel(float x, float y, float w, float h,
                           u32 fill, u32 accent, float chamfer);
static void draw_tab(float x, float y, float w, float h,
                     u32 fill, u32 accent, int active);
static void segmented_bar_impl(float x, float y, float w, float h,
                               float fraction, u32 color, int segments);

// ---------------------------------------------------------------------------
// Theme API implementation
// ---------------------------------------------------------------------------

void graphics_apply_theme(int idx)
{
    if (idx < 0 || idx >= s_theme_count) idx = 0;
    const ThemePreset *t = &s_themes[idx];

    clrBg       = C2D_Color32(t->bg_r,  t->bg_g,  t->bg_b,  255);
    clrPanel    = C2D_Color32(t->pan_r, t->pan_g, t->pan_b, 255);
    clrPanelDim = C2D_Color32(t->dim_r, t->dim_g, t->dim_b, 255);
    clrText     = C2D_Color32(230, 230, 220, 255);
    clrTextDim  = C2D_Color32(140, 150, 150, 255);
    clrAmber    = C2D_Color32(t->ac1_r, t->ac1_g, t->ac1_b, 255);
    clrCyan     = C2D_Color32(t->ac2_r, t->ac2_g, t->ac2_b, 255);
    clrDanger   = C2D_Color32(t->dng_r, t->dng_g, t->dng_b, 255);

    s_colors.bg       = clrBg;
    s_colors.panel    = clrPanel;
    s_colors.panelDim = clrPanelDim;
    s_colors.text     = clrText;
    s_colors.textDim  = clrTextDim;
    s_colors.amber    = clrAmber;
    s_colors.cyan     = clrCyan;
    s_colors.danger   = clrDanger;
}

const ThemeColors *graphics_get_colors(void) { return &s_colors; }
int  graphics_theme_count(void)              { return s_theme_count; }
const char *graphics_theme_name(int idx)
{
    if (idx < 0 || idx >= s_theme_count) return "?";
    return s_themes[idx].name;
}

ThemePreset *graphics_get_theme_mut(int idx)
{
    if (idx < 0 || idx >= s_theme_count) return NULL;
    return &s_themes[idx];
}

int graphics_add_theme(void)
{
    if (s_theme_count >= MAX_THEMES) return s_theme_count - 1;
    s_themes[s_theme_count] = s_themes[g_theme_index]; // duplicate active
    s_themes[s_theme_count].name[0] = 'C';
    s_themes[s_theme_count].name[1] = 'S';
    s_themes[s_theme_count].name[2] = 'T';
    s_themes[s_theme_count].name[3] = '0' + (char)(s_theme_count % 10);
    s_themes[s_theme_count].name[4] = '\0';
    return s_theme_count++;
}

void graphics_delete_theme(int idx)
{
    if (s_theme_count <= 1 || idx < 0 || idx >= s_theme_count) return;
    for (int i = idx; i < s_theme_count - 1; i++)
        s_themes[i] = s_themes[i + 1];
    s_theme_count--;
    if (g_theme_index >= s_theme_count) g_theme_index = s_theme_count - 1;
    graphics_apply_theme(g_theme_index);
}

void graphics_load_themes(void)
{
    FILE *f = fopen("sdmc:/sysmon_theme.txt", "r");
    if (!f) return; // keep factory defaults

    char line[128];
    int active = 0, count = 0;

    // First line: "# active count"
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; }
    if (line[0] == '#') sscanf(line + 1, "%d %d", &active, &count);

    if (count < 1 || count > MAX_THEMES) { fclose(f); return; }

    int loaded = 0;
    while (loaded < count && fgets(line, sizeof(line), f))
    {
        if (line[0] == '#') continue;
        ThemePreset *t = &s_themes[loaded];
        int r = sscanf(line,
            "%15s "
            "%hhu %hhu %hhu "
            "%hhu %hhu %hhu "
            "%hhu %hhu %hhu "
            "%hhu %hhu %hhu "
            "%hhu %hhu %hhu "
            "%hhu %hhu %hhu",
            t->name,
            &t->bg_r,  &t->bg_g,  &t->bg_b,
            &t->pan_r, &t->pan_g, &t->pan_b,
            &t->dim_r, &t->dim_g, &t->dim_b,
            &t->ac1_r, &t->ac1_g, &t->ac1_b,
            &t->ac2_r, &t->ac2_g, &t->ac2_b,
            &t->dng_r, &t->dng_g, &t->dng_b);
        if (r == 19) loaded++;
    }
    fclose(f);

    if (loaded > 0)
    {
        s_theme_count = loaded;
        g_theme_index = (active >= 0 && active < s_theme_count) ? active : 0;
    }
}

void graphics_save_themes(void)
{
    FILE *f = fopen("sdmc:/sysmon_theme.txt", "w");
    if (!f) return;
    fprintf(f, "# %d %d\n", g_theme_index, s_theme_count);
    for (int i = 0; i < s_theme_count; i++)
    {
        const ThemePreset *t = &s_themes[i];
        fprintf(f, "%s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
                t->name,
                t->bg_r,  t->bg_g,  t->bg_b,
                t->pan_r, t->pan_g, t->pan_b,
                t->dim_r, t->dim_g, t->dim_b,
                t->ac1_r, t->ac1_g, t->ac1_b,
                t->ac2_r, t->ac2_g, t->ac2_b,
                t->dng_r, t->dng_g, t->dng_b);
    }
    fclose(f);
}

void graphics_draw_theme_preview(float x, float y, float w, float h, int idx)
{
    if (idx < 0 || idx >= s_theme_count) return;
    const ThemePreset *t = &s_themes[idx];
    u32 fill   = C2D_Color32(t->pan_r, t->pan_g, t->pan_b, 255);
    u32 accent = C2D_Color32(t->ac1_r, t->ac1_g, t->ac1_b, 255);
    u32 ac2    = C2D_Color32(t->ac2_r, t->ac2_g, t->ac2_b, 255);
    draw_hud_panel(x, y, w, h, fill, accent, 4);
    // Two accent colour dots on the right side
    C2D_DrawCircleSolid(x + w - 8,  y + h * 0.5f, 0, 4, accent);
    C2D_DrawCircleSolid(x + w - 18, y + h * 0.5f, 0, 4, ac2);
}

// ---------------------------------------------------------------------------
// Init / exit
// ---------------------------------------------------------------------------
static void parse_static_text(C2D_Text *textObj, const char *str)
{
    if (customFont) C2D_TextFontParse(textObj, customFont, staticBuf, str);
    else            C2D_TextParse(textObj, staticBuf, str);
    C2D_TextOptimize(textObj);
}

void graphics_init(void)
{
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    top    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    dynamicBuf = C2D_TextBufNew(4096);
    staticBuf  = C2D_TextBufNew(4096);
    customFont = C2D_FontLoad("romfs:/minecraft.bcfnt");

    // Load theme data first, then apply
    graphics_load_themes();
    graphics_apply_theme(g_theme_index);

    parse_static_text(&txt_cpu,       "CPU");
    parse_static_text(&txt_gpu,       "GPU");
    parse_static_text(&txt_history,   "HISTORY");
    parse_static_text(&txt_pomo_tab,  "POMO");
    parse_static_text(&txt_kill_tab,  "KILL");
    parse_static_text(&txt_macro_tab, "MACRO");
    parse_static_text(&txt_media_tab, "MEDIA");
    parse_static_text(&txt_set_tab,   "SET");
    parse_static_text(&txt_level_tab, "LEVEL");
}

void graphics_exit(void)
{
    if (customFont) C2D_FontFree(customFont);
    C2D_TextBufDelete(dynamicBuf);
    C2D_TextBufDelete(staticBuf);
    C2D_Fini();
    C3D_Fini();
}

// ---------------------------------------------------------------------------
// Public draw helpers
// ---------------------------------------------------------------------------
static u32 get_temp_color(float temp)
{
    if (temp < 60.0f) return clrCyan;
    if (temp < 80.0f) return clrAmber;
    return clrDanger;
}

void graphics_draw_dynamic_text(C2D_Text *textObj, const char *str,
                                float x, float y, float scale, u32 color)
{
    if (customFont) C2D_TextFontParse(textObj, customFont, dynamicBuf, str);
    else            C2D_TextParse(textObj, dynamicBuf, str);
    C2D_TextOptimize(textObj);
    C2D_DrawText(textObj, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

void graphics_draw_static_text(C2D_Text *textObj, float x, float y, float scale, u32 color)
{
    C2D_DrawText(textObj, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

void graphics_draw_hud_panel(float x, float y, float w, float h,
                             u32 fill, u32 accent, float chamfer)
{
    draw_hud_panel(x, y, w, h, fill, accent, chamfer);
}

void graphics_draw_segmented_bar(float x, float y, float w, float h,
                                 float fraction, u32 color, int segments)
{
    segmented_bar_impl(x, y, w, h, fraction, color, segments);
}

// ---------------------------------------------------------------------------
// Tab drawer
// ---------------------------------------------------------------------------
static C2D_Text *tab_text_for_mode(int mode)
{
    switch (mode) {
    case 0: return &txt_pomo_tab;
    case 1: return &txt_kill_tab;
    case 2: return &txt_macro_tab;
    case 3: return &txt_media_tab;
    case 4: return &txt_set_tab;
    case 5: return &txt_level_tab;
    default: return &txt_pomo_tab;
    }
}

int graphics_tab_count(void)
{
    return (int)(sizeof(tabEntries) / sizeof(tabEntries[0]));
}

static u32 tab_accent_for_mode(int mode)
{
    switch (mode) {
    case 0: return clrAmber;
    case 1: return clrDanger;
    case 2: return clrAmber;
    case 3: return clrCyan;
    case 4: return clrCyan;
    case 5: return clrAmber;
    default: return clrAmber;
    }
}

static int clamp_tab_scroll(int scroll)
{
    int max_scroll = graphics_tab_count() - TAB_DRAWER_SCROLL_VISIBLE_ROWS;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll < 0) return 0;
    if (scroll > max_scroll) return max_scroll;
    return scroll;
}

int graphics_tab_uses_scroll(void)
{
    return graphics_tab_count() > TAB_DRAWER_NOSCROLL_MAX;
}

int graphics_tab_touch_hit(float px, float py, int scroll, int *out_scroll)
{
    if (px < 270.0f || px > 320.0f || py < 0.0f || py > 240.0f) return -1;

    int total = graphics_tab_count();
    if (!graphics_tab_uses_scroll())
    {
        float row_h = 240.0f / (float)total;
        int index = (int)(py / row_h);
        if (index < 0 || index >= total) return -1;
        return tabEntries[index].mode;
    }

    scroll = clamp_tab_scroll(scroll);
    int max_scroll = total - TAB_DRAWER_SCROLL_VISIBLE_ROWS;
    if (max_scroll < 0) max_scroll = 0;

    if (py < TAB_DRAWER_ARROW_ZONE)
    {
        if (scroll > 0 && out_scroll) { *out_scroll = scroll - 1; return -2; }
        return -1;
    }

    float rows_bottom = TAB_DRAWER_ARROW_ZONE + TAB_DRAWER_SCROLL_ROW_H * TAB_DRAWER_SCROLL_VISIBLE_ROWS;
    if (py >= rows_bottom)
    {
        if (scroll < max_scroll && out_scroll) { *out_scroll = scroll + 1; return -2; }
        return -1;
    }

    int slot  = (int)((py - TAB_DRAWER_ARROW_ZONE) / TAB_DRAWER_SCROLL_ROW_H);
    int index = scroll + slot;
    if (slot < 0 || slot >= TAB_DRAWER_SCROLL_VISIBLE_ROWS || index >= total) return -1;
    return tabEntries[index].mode;
}

static void draw_scroll_marker(float x, float y, int up, u32 color)
{
    float vx[3], vy[3];
    if (up) {
        vx[0]=x+10; vy[0]=y+6;  vx[1]=x+4;  vy[1]=y+14; vx[2]=x+16; vy[2]=y+14;
    } else {
        vx[0]=x+10; vy[0]=y+14; vx[1]=x+4;  vy[1]=y+6;  vx[2]=x+16; vy[2]=y+6;
    }
    fill_polygon(vx, vy, 3, color);
}

void graphics_draw_tab_drawer(int active_mode, int scroll)
{
    int total = graphics_tab_count();
    draw_hud_panel(270, 0, 50, 240, clrPanel, clrAmber, 10);

    if (!graphics_tab_uses_scroll())
    {
        float row_h = 240.0f / (float)total;
        float label_scale = (row_h < 40.0f) ? 0.36f : 0.42f;
        for (int i = 0; i < total; i++)
        {
            float y   = i * row_h;
            int mode  = tabEntries[i].mode;
            int active = (mode == active_mode);
            draw_tab(270, y, 50, row_h, active ? clrPanelDim : clrPanel,
                     tab_accent_for_mode(mode), active);
            graphics_draw_static_text(tab_text_for_mode(mode),
                                      282, y + (row_h * 0.5f) - 6.0f, label_scale, clrText);
        }
        return;
    }

    scroll = clamp_tab_scroll(scroll);
    int max_scroll = total - TAB_DRAWER_SCROLL_VISIBLE_ROWS;
    if (max_scroll < 0) max_scroll = 0;

    u32 up_color   = (scroll > 0)          ? clrAmber : clrPanelDim;
    u32 down_color = (scroll < max_scroll) ? clrAmber : clrPanelDim;
    float rows_bottom = TAB_DRAWER_ARROW_ZONE + TAB_DRAWER_SCROLL_ROW_H * TAB_DRAWER_SCROLL_VISIBLE_ROWS;

    draw_scroll_marker(277, 2, 1, up_color);
    draw_scroll_marker(277, rows_bottom + 2, 0, down_color);
    C2D_DrawRectSolid(270, TAB_DRAWER_ARROW_ZONE - 1, 0, 50, 1, clrPanelDim);
    C2D_DrawRectSolid(270, rows_bottom,               0, 50, 1, clrPanelDim);

    for (int slot = 0; slot < TAB_DRAWER_SCROLL_VISIBLE_ROWS; slot++)
    {
        int index = scroll + slot;
        if (index >= total) break;
        int y     = (int)TAB_DRAWER_ARROW_ZONE + slot * (int)TAB_DRAWER_SCROLL_ROW_H;
        int mode  = tabEntries[index].mode;
        int active = (mode == active_mode);
        draw_tab(270, y, 50, TAB_DRAWER_SCROLL_ROW_H,
                 active ? clrPanelDim : clrPanel, tab_accent_for_mode(mode), active);
        graphics_draw_static_text(tab_text_for_mode(mode), 282, y + 16, 0.42f, clrText);
    }
}

// ---------------------------------------------------------------------------
// NMS-style primitives
// ---------------------------------------------------------------------------
static void fill_polygon(const float *vx, const float *vy, int n, u32 color)
{
    float cx = 0.0f, cy = 0.0f;
    for (int i = 0; i < n; i++) { cx += vx[i]; cy += vy[i]; }
    cx /= n; cy /= n;
    for (int i = 0; i < n; i++)
    {
        int j = (i + 1) % n;
        C2D_DrawTriangle(cx, cy, color, vx[i], vy[i], color, vx[j], vy[j], color, 0.0f);
    }
}

static float pulse01(float speed)
{
    float t = (float)(osGetTime() % 1000000) / 1000.0f;
    return (sinf(t * speed) + 1.0f) * 0.5f;
}

static void draw_hud_panel(float x, float y, float w, float h,
                           u32 fill, u32 accent, float chamfer)
{
    float vx[6] = {x+chamfer, x+w, x+w, x+w-chamfer, x, x};
    float vy[6] = {y, y, y+h-chamfer, y+h, y+h, y+chamfer};
    fill_polygon(vx, vy, 6, fill);
    C2D_DrawLine(vx[5],vy[5],accent,vx[0],vy[0],accent,1.5f,0);
    C2D_DrawLine(vx[2],vy[2],accent,vx[3],vy[3],accent,1.5f,0);
    float bl = (w < 60.0f || h < 60.0f) ? 6.0f : 10.0f;
    C2D_DrawLine(x+w-bl,y,    accent,x+w,y,    accent,1.5f,0);
    C2D_DrawLine(x+w,   y,    accent,x+w,y+bl, accent,1.5f,0);
    C2D_DrawLine(x,     y+h-bl,accent,x,  y+h, accent,1.5f,0);
    C2D_DrawLine(x,     y+h,  accent,x+bl,y+h, accent,1.5f,0);
}

static void draw_tab(float x, float y, float w, float h, u32 fill, u32 accent, int active)
{
    float c = h * 0.28f;
    float vx[5] = {x+c, x+w, x+w, x+c, x};
    float vy[5] = {y, y, y+h, y+h, y+h/2.0f};
    fill_polygon(vx, vy, 5, fill);
    if (active)
    {
        C2D_DrawLine(vx[4],vy[4],accent,vx[0],vy[0],accent,2.0f,0);
        C2D_DrawLine(vx[4],vy[4],accent,vx[3],vy[3],accent,2.0f,0);
        float px[3]={x-6.0f,x,x}, py[3]={y+h/2.0f,y+h/2.0f-5.0f,y+h/2.0f+5.0f};
        fill_polygon(px, py, 3, accent);
    }
}

static void segmented_bar_impl(float x, float y, float w, float h,
                               float fraction, u32 color, int segments)
{
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    float gap   = 2.0f;
    float seg_w = (w - gap * (segments - 1)) / segments;
    int filled  = (int)(fraction * segments + 0.5f);
    for (int i = 0; i < segments; i++)
    {
        float sx = x + i * (seg_w + gap);
        C2D_DrawRectSolid(sx, y, 0, seg_w, h, (i < filled) ? color : clrPanelDim);
    }
}

static void draw_scanlines(float x, float y, float w, float h, u8 alpha)
{
    for (float ly = y; ly < y + h; ly += 4.0f)
        C2D_DrawRectSolid(x, ly, 0, w, 1, C2D_Color32(255, 255, 255, alpha));
}

static void draw_ring_segment(float cx, float cy, float inner_r, float outer_r,
                              float start_angle, float end_angle, u32 color)
{
    float vx[4], vy[4];
    vx[0]=cx+cosf(start_angle)*outer_r; vy[0]=cy-sinf(start_angle)*outer_r;
    vx[1]=cx+cosf(end_angle)*outer_r;   vy[1]=cy-sinf(end_angle)*outer_r;
    vx[2]=cx+cosf(end_angle)*inner_r;   vy[2]=cy-sinf(end_angle)*inner_r;
    vx[3]=cx+cosf(start_angle)*inner_r; vy[3]=cy-sinf(start_angle)*inner_r;
    fill_polygon(vx, vy, 4, color);
}

void graphics_draw_throttle_dial(float cx, float cy, float outer_r, float inner_r,
                                 float level, u32 fill, u32 accent)
{
    const float start_angle = 2.3561945f;
    const float sweep       = 4.7123890f;
    const int   segments    = 28;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    for (int i = 0; i < segments; i++)
    {
        float t1 = (float)i       / (float)segments;
        float t2 = (float)(i + 1) / (float)segments;
        float a1 = start_angle + sweep * t1;
        float a2 = start_angle + sweep * t2;
        draw_ring_segment(cx, cy, inner_r, outer_r, a1, a2, (t2 <= level) ? accent : fill);
    }
    C2D_DrawCircleSolid(cx, cy, 0.0f, inner_r - 7.0f, clrBg);
    C2D_DrawCircleSolid(cx, cy, 0.0f, 3.0f, accent);
}

// ---------------------------------------------------------------------------
// Main frame draw
// ---------------------------------------------------------------------------
void graphics_draw_frame(int set_sub, int set_row, int preview_idx,
                         int editing, int edit_field, int edit_swatch)
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    float glow        = pulse01(2.0f);
    u8    bracketAlpha = (u8)(160 + glow * 95);

    // accentPulse uses live theme colour with variable alpha
    u32 accentPulse = C2D_Color32(
        s_colors.amber & 0xFF,
        (s_colors.amber >> 8)  & 0xFF,
        (s_colors.amber >> 16) & 0xFF,
        bracketAlpha);

    // ============================= TOP SCREEN =============================
    C2D_TargetClear(top, clrBg);
    C2D_SceneBegin(top);
    C2D_TextBufClear(dynamicBuf);

    char textStr[256];
    C2D_Text textObj;

    if (g_cpu_temp > 85.0f)
    {
        u8 a = (u8)(40 + glow * 60);
        u32 dng_a = C2D_Color32(
            s_colors.danger & 0xFF,
            (s_colors.danger >> 8)  & 0xFF,
            (s_colors.danger >> 16) & 0xFF, a);
        C2D_DrawRectSolid(0,   0,   0, 400, 6, dng_a);
        C2D_DrawRectSolid(0, 234,   0, 400, 6, dng_a);
    }

    draw_scanlines(0, 0, 400, 240, 4);

    draw_hud_panel(10,  10, 185, 90, clrPanel, clrAmber,     14);
    draw_hud_panel(205, 10, 185, 90, clrPanel, clrCyan,      14);
    draw_hud_panel(10, 110, 250, 90, clrPanel, accentPulse,  14);
    draw_hud_panel(270,110, 120, 90, clrPanel, clrAmber,     12);

    graphics_draw_static_text(&txt_cpu,     24,  18, 0.55f, clrTextDim);
    graphics_draw_static_text(&txt_gpu,     219, 18, 0.55f, clrTextDim);
    graphics_draw_static_text(&txt_history, 24, 118, 0.55f, clrTextDim);

    snprintf(textStr, sizeof(textStr), "%.1f%%", g_cpu_usage);
    graphics_draw_dynamic_text(&textObj, textStr, 24, 38, 0.6f, clrText);
    graphics_draw_segmented_bar(24, 60, 155, 8, g_cpu_usage / 100.0f, clrAmber, 20);
    snprintf(textStr, sizeof(textStr), "%.1fC  FAN %d", g_cpu_temp, g_cpu_fan);
    graphics_draw_dynamic_text(&textObj, textStr, 24, 75, 0.42f, clrTextDim);
    graphics_draw_segmented_bar(24, 88, 155, 6, g_cpu_temp / 100.0f, get_temp_color(g_cpu_temp), 20);

    snprintf(textStr, sizeof(textStr), "%.1fC  FAN %d", g_gpu_temp, g_gpu_fan);
    graphics_draw_dynamic_text(&textObj, textStr, 219, 38, 0.45f, clrText);
    graphics_draw_segmented_bar(219, 58, 155, 8, g_gpu_temp / 100.0f, get_temp_color(g_gpu_temp), 20);
    snprintf(textStr, sizeof(textStr), "FREE RAM  %.2f GB", g_free_ram);
    graphics_draw_dynamic_text(&textObj, textStr, 219, 75, 0.42f, clrTextDim);

    if (g_history_count > 1)
    {
        float lo = 1000.0f, hi = -1000.0f;
        for (int i = 0; i < g_history_count; i++)
        {
            if (g_temp_history[i]     < lo) lo = g_temp_history[i];
            if (g_temp_history[i]     > hi) hi = g_temp_history[i];
            if (g_gpu_temp_history[i] < lo) lo = g_gpu_temp_history[i];
            if (g_gpu_temp_history[i] > hi) hi = g_gpu_temp_history[i];
        }
        float span = (hi - lo < 5.0f) ? 5.0f : (hi - lo);
        float sx = 24, sy = 185, gh = 45.0f, dx = 230.0f / 9.0f;
        for (int i = 0; i < g_history_count - 1; i++)
        {
            float x1=sx+i*dx, x2=sx+(i+1)*dx;
            float y1=sy-((g_temp_history[i]    -lo)/span*gh);
            float y2=sy-((g_temp_history[i+1]  -lo)/span*gh);
            float gy1=sy-((g_gpu_temp_history[i]  -lo)/span*gh);
            float gy2=sy-((g_gpu_temp_history[i+1]-lo)/span*gh);
            C2D_DrawLine(x1,y1,clrAmber,x2,y2,clrAmber,2.0f,0);
            C2D_DrawLine(x1,gy1,clrCyan, x2,gy2,clrCyan, 2.0f,0);
        }
    }

    time_t t = time(NULL);
    struct tm *tmv = localtime(&t);
    char clock_str[8];
    snprintf(clock_str, sizeof(clock_str), "%02d:%02d", tmv->tm_hour, tmv->tm_min);
    if (strcmp(last_clock, clock_str) != 0)
        snprintf(last_clock, sizeof(last_clock), "%s", clock_str);
    graphics_draw_dynamic_text(&textObj, last_clock, 282, 128, 1.15f, clrText);
    graphics_draw_dynamic_text(&textObj, g_weather,  282, 168, 0.45f, clrTextDim);

    u32 statusColor;
    const char *statusText;
    if      (g_http_status == 1) { statusColor = clrCyan;   statusText = "TCP: CONNECTED";  }
    else if (g_http_status == 0) { statusColor = clrAmber;  statusText = "TCP: CONNECTING"; }
    else                         { statusColor = clrDanger; statusText = "TCP: ERROR";       }
    graphics_draw_dynamic_text(&textObj, statusText, 12, 208, 0.42f, statusColor);

    if (!g_fetching_enabled)
        graphics_draw_dynamic_text(&textObj, "(PAUSED)", 145, 208, 0.42f, clrAmber);

    u8 battery_level = 0, is_charging = 0;
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
        graphics_draw_dynamic_text(&textObj, textStr, 332, 208, 0.4f,
                                   battery_level <= 1 ? clrDanger : clrAmber);
    }

    if (g_has_notification)
    {
        draw_hud_panel(0, 0, 400, 26,
                       C2D_Color32(
                           (s_colors.bg & 0xFF) + 22,
                           ((s_colors.bg >> 8) & 0xFF) + 8,
                           ((s_colors.bg >> 16) & 0xFF) + 0,
                           235),
                       clrAmber, 10);
        graphics_draw_dynamic_text(&textObj, "DESKTOP NOTIFICATION", 90, 6, 0.6f, clrAmber);
    }

    // ============================ BOTTOM SCREEN ============================
    C2D_TargetClear(bottom, clrBg);
    C2D_SceneBegin(bottom);
    draw_scanlines(0, 0, 320, 240, 3);

    if (g_kill_confirm_pid > 0)
    {
        draw_hud_panel(20, 60, 230, 120, clrPanel, clrDanger, 16);
        snprintf(textStr, sizeof(textStr), "KILL %s?", g_kill_confirm_name);
        graphics_draw_dynamic_text(&textObj, textStr,    34, 82,  0.6f,  clrDanger);
        graphics_draw_dynamic_text(&textObj, "(A) CONFIRM", 54, 122, 0.5f, clrAmber);
        graphics_draw_dynamic_text(&textObj, "(B) CANCEL",  54, 142, 0.5f, clrTextDim);
    }
    else if (g_screen_mode == 0) { graphics_draw_pomo_tab(glow); }
    else if (g_screen_mode == 1) { graphics_draw_kill_tab(); }
    else if (g_screen_mode == 2) { graphics_draw_macro_tab(); }
    else if (g_screen_mode == 3) { graphics_draw_media_tab(); }
    else if (g_screen_mode == 4)
    {
        if      (set_sub == 1) graphics_draw_settings_manager(set_row, preview_idx);
        else if (set_sub == 2) graphics_draw_settings_editor(set_row, editing, edit_field, edit_swatch);
        else                   graphics_draw_settings_tab(set_row, preview_idx);
    }
    else if (g_screen_mode == 5) { graphics_draw_level_tab(); }

    graphics_draw_tab_drawer(g_screen_mode, g_tab_scroll);
    C3D_FrameEnd(0);
}