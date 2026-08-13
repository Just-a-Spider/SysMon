#pragma once
#include <citro2d.h>

// ---------------------------------------------------------------------------
// Tab drawer
// ---------------------------------------------------------------------------
#define TAB_DRAWER_VISIBLE_ROWS 5

int  graphics_tab_count(void);
int  graphics_tab_uses_scroll(void);
int  graphics_tab_touch_hit(float px, float py, int scroll, int *out_scroll);

// ---------------------------------------------------------------------------
// Theme preset (public so settings_tab.c can read field values for swatches)
// ---------------------------------------------------------------------------
#define MAX_THEMES 8

typedef struct {
    char name[16];
    // background / panels
    u8 bg_r,  bg_g,  bg_b;
    u8 pan_r, pan_g, pan_b;
    u8 dim_r, dim_g, dim_b;
    // accent colours
    u8 ac1_r, ac1_g, ac1_b;  // primary   (amber slot)
    u8 ac2_r, ac2_g, ac2_b;  // secondary (cyan slot)
    u8 dng_r, dng_g, dng_b;  // danger
} ThemePreset;

// Live palette — updated every time apply_theme() is called
typedef struct {
    u32 bg, panel, panelDim;
    u32 text, textDim;
    u32 amber, cyan, danger;
} ThemeColors;

// ---------------------------------------------------------------------------
// Theme API
// ---------------------------------------------------------------------------
extern int g_theme_index;

const ThemeColors  *graphics_get_colors(void);
int                 graphics_theme_count(void);
void                graphics_apply_theme(int idx);
ThemePreset        *graphics_get_theme_mut(int idx);   // for in-place editing
int                 graphics_add_theme(void);           // duplicate active → new slot
void                graphics_delete_theme(int idx);
void                graphics_load_themes(void);
void                graphics_save_themes(void);
const char         *graphics_theme_name(int idx);
void                graphics_palette_color(int idx, u8 *r, u8 *g, u8 *b);
int                 graphics_palette_count(void);

// Draws a mini HUD-panel preview using raw preset colours (works for non-active presets)
void graphics_draw_theme_preview(float x, float y, float w, float h, int theme_idx);

// ---------------------------------------------------------------------------
// Primitive draw helpers (used by tab files)
// ---------------------------------------------------------------------------
void graphics_draw_dynamic_text(C2D_Text *textObj, const char *str,
                                float x, float y, float scale, u32 color);
void graphics_draw_static_text(C2D_Text *textObj,
                               float x, float y, float scale, u32 color);
void graphics_draw_hud_panel(float x, float y, float w, float h,
                             u32 fill, u32 accent, float chamfer);
void graphics_draw_segmented_bar(float x, float y, float w, float h,
                                 float fraction, u32 color, int segments);
void graphics_draw_throttle_dial(float cx, float cy, float outer_r, float inner_r,
                                 float level, u32 fill, u32 accent);

// ---------------------------------------------------------------------------
// Tab draw functions
// ---------------------------------------------------------------------------
void graphics_draw_pomo_tab(float glow);
void graphics_draw_level_tab(void);
void graphics_draw_kill_tab(void);
void graphics_draw_macro_tab(void);
void graphics_draw_media_tab(void);

// SET tab — three sub-views (dispatched by graphics_draw_frame based on g_set_sub)
// set_row: 0=THEME 1=SERVER
// sub: 0=main 1=manager 2=editor
void graphics_draw_settings_tab(int set_row, int preview_idx);
void graphics_draw_settings_manager(int set_row, int preview_idx);
void graphics_draw_settings_editor(int set_row, int editing,
                                   int edit_field, int edit_swatch);

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void graphics_init(void);
void graphics_exit(void);
void graphics_draw_tab_drawer(int active_mode, int scroll);

// SET state passed in so graphics.c stays stateless w.r.t. navigation
void graphics_draw_frame(int set_sub, int set_row, int preview_idx,
                         int editing, int edit_field, int edit_swatch);
