#pragma once
#include <citro2d.h>

#define TAB_DRAWER_VISIBLE_ROWS 5

int graphics_tab_count();
int graphics_tab_uses_scroll(void);
int graphics_tab_touch_hit(float px, float py, int scroll, int *out_scroll);
void graphics_draw_dynamic_text(C2D_Text *textObj, const char *str, float x, float y, float scale, u32 color);
void graphics_draw_static_text(C2D_Text *textObj, float x, float y, float scale, u32 color);
void graphics_draw_hud_panel(float x, float y, float w, float h, u32 fill, u32 accent, float chamfer);
void graphics_draw_segmented_bar(float x, float y, float w, float h, float fraction, u32 color, int segments);
void graphics_draw_throttle_dial(float cx, float cy, float outer_r, float inner_r, float level, u32 fill, u32 accent);
void graphics_draw_pomo_tab(float glow);
void graphics_draw_level_tab(void);
void graphics_draw_kill_tab(void);
void graphics_draw_macro_tab(void);
void graphics_draw_media_tab(void);
void graphics_draw_settings_tab(const char *ip_buffer, int port);

void graphics_init();
void graphics_exit();
void graphics_draw_tab_drawer(int active_mode, int scroll);
void graphics_draw_frame(const char *ip_buffer, int port);
