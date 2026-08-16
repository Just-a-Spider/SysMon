#ifndef DISABLE_CAM
#include "graphics.h"
#include "network.h"
#include "net_cam.h"
#include <stdio.h>
#include <time.h>
#include <math.h>

// 3DS PICA200 GPU Texture and Citro2D image
static C3D_Tex s_cam_tex;
static C2D_Image s_cam_img;
static Tex3DS_SubTexture s_cam_subtex;
static int s_cam_tex_inited = 0;
static u16 *s_tex_linear_buf = NULL;
static u8 s_morton_lut[8][8];
static int s_morton_inited = 0;

static int s_last_frame_w = 0;
static int s_last_frame_h = 0;
static u64 s_frame_count = 0;

static void init_morton_lut(void)
{
    if (s_morton_inited) return;
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            s_morton_lut[y][x] = (x & 1) | ((y & 1) << 1) |
                                 ((x & 2) << 1) | ((y & 2) << 2) |
                                 ((x & 4) << 2) | ((y & 4) << 3);
        }
    }
    s_morton_inited = 1;
}

void graphics_cam_init(void)
{
    if (s_cam_tex_inited) return;
    init_morton_lut();

    // 512x256 GPU_RGB565 texture (covers both 240x160 and 400x240)
    C3D_TexInit(&s_cam_tex, 512, 256, GPU_RGB565);
    C3D_TexSetFilter(&s_cam_tex, GPU_LINEAR, GPU_LINEAR);

    s_cam_subtex.width = 240;
    s_cam_subtex.height = 160;
    s_cam_subtex.left = 0.0f;
    s_cam_subtex.top = 1.0f;
    s_cam_subtex.right = 240.0f / 512.0f;
    s_cam_subtex.bottom = 1.0f - (160.0f / 256.0f);

    s_cam_img.tex = &s_cam_tex;
    s_cam_img.subtex = &s_cam_subtex;

    s_tex_linear_buf = (u16 *)s_cam_tex.data;
    s_cam_tex_inited = 1;
}

void graphics_cam_exit(void)
{
    if (s_cam_tex_inited)
    {
        C3D_TexDelete(&s_cam_tex);
        s_cam_tex_inited = 0;
        s_tex_linear_buf = NULL;
    }
}

void graphics_cam_update_frame(const u16 *rgb565, int width, int height)
{
    if (!s_cam_tex_inited || !s_tex_linear_buf || !rgb565 || width <= 0 || height <= 0) return;

    if (width > 512) width = 512;
    if (height > 256) height = 256;

    const int tex_w = 512;
    // Fast 8x8 morton tiling into GPU texture buffer
    for (int y = 0; y < height; y++)
    {
        int tile_y = y >> 3;
        int in_y = y & 7;
        int row_offset = y * width;
        int tile_y_offset = tile_y * (tex_w >> 3);

        for (int x = 0; x < width; x++)
        {
            int tile_x = x >> 3;
            int in_x = x & 7;
            int dst_idx = (tile_y_offset + tile_x) * 64 + s_morton_lut[in_y][in_x];
            s_tex_linear_buf[dst_idx] = rgb565[row_offset + x];
        }
    }

    s_cam_subtex.width = width;
    s_cam_subtex.height = height;
    s_cam_subtex.left = 0.0f;
    s_cam_subtex.top = 1.0f;
    s_cam_subtex.right = (float)width / 512.0f;
    s_cam_subtex.bottom = 1.0f - ((float)height / 256.0f);

    s_last_frame_w = width;
    s_last_frame_h = height;
    GSPGPU_FlushDataCache(s_cam_tex.data, s_cam_tex.size);
    s_frame_count++;
}

void graphics_cam_update_delta_tiles(const u8 *tile_payload, u16 tile_count, int width, int height)
{
    if (!s_cam_tex_inited || !s_tex_linear_buf || !tile_payload || width <= 0 || height <= 0) return;

    if (width > 512) width = 512;
    if (height > 256) height = 256;

    const int tiles_per_row = 512 >> 3; // 64 tiles per row in 512x256 texture
    const u8 *ptr = tile_payload;

    for (u16 i = 0; i < tile_count; i++)
    {
        u8 tx = *ptr++;
        u8 ty = *ptr++;
        if (tx < 64 && ty < 32)
        {
            size_t dst_offset = ((size_t)ty * tiles_per_row + tx) * 64; // 64 u16 elements = 128 bytes
            memcpy(s_tex_linear_buf + dst_offset, ptr, 128);
        }
        ptr += 128;
    }

    if (s_last_frame_w != width || s_last_frame_h != height)
    {
        s_cam_subtex.width = width;
        s_cam_subtex.height = height;
        s_cam_subtex.left = 0.0f;
        s_cam_subtex.top = 1.0f;
        s_cam_subtex.right = (float)width / 512.0f;
        s_cam_subtex.bottom = 1.0f - ((float)height / 256.0f);
        s_last_frame_w = width;
        s_last_frame_h = height;
    }

    GSPGPU_FlushDataCache(s_cam_tex.data, s_cam_tex.size);
    s_frame_count++;
}

static void draw_cam_brackets(float x, float y, float w, float h, float len, u32 color)
{
    // Top-left
    C2D_DrawLine(x, y, color, x + len, y, color, 2.0f, 0);
    C2D_DrawLine(x, y, color, x, y + len, color, 2.0f, 0);
    // Top-right
    C2D_DrawLine(x + w, y, color, x + w - len, y, color, 2.0f, 0);
    C2D_DrawLine(x + w, y, color, x + w, y + len, color, 2.0f, 0);
    // Bottom-left
    C2D_DrawLine(x, y + h, color, x + len, y + h, color, 2.0f, 0);
    C2D_DrawLine(x, y + h, color, x, y + h - len, color, 2.0f, 0);
    // Bottom-right
    C2D_DrawLine(x + w, y + h, color, x + w - len, y + h, color, 2.0f, 0);
    C2D_DrawLine(x + w, y + h, color, x + w, y + h - len, color, 2.0f, 0);
}

// ---------------------------------------------------------------------------
// Bottom Tab Cam Drawing
// ---------------------------------------------------------------------------
void graphics_draw_cam_tab(void)
{
    const ThemeColors *tc = graphics_get_colors();
    C2D_Text textObj;
    char str[128];

    // Viewport coordinates
    const float vx = 14.0f;
    const float vy = 12.0f;
    const float vw = 240.0f;
    const float vh = 160.0f;

    // HUD Container Panel
    graphics_draw_hud_panel(vx - 4.0f, vy - 4.0f, vw + 8.0f, vh + 8.0f, tc->panelDim, tc->amber, 8.0f);

    u64 now = osGetTime();
    int is_standby = (strcmp(g_cam_source_name, "STANDBY") == 0);
    int has_signal = (s_frame_count > 0 && (now - g_cam_last_frame_time < 3000) && !is_standby);

    if (g_cam_top_screen)
    {
        // Bottom screen acts as spectator control HUD when Top Screen displays video
        C2D_DrawRectSolid(vx, vy, 0, vw, vh, C2D_Color32(10, 15, 20, 255));
        graphics_draw_dynamic_text(&textObj, "SPECTATOR ACTIVE", vx + 45, vy + 50, 0.55f, tc->cyan);
        graphics_draw_dynamic_text(&textObj, "Feed rendered on Top Screen", vx + 35, vy + 75, 0.40f, tc->text);
        snprintf(str, sizeof(str), "SOURCE: %s", g_cam_source_name);
        graphics_draw_dynamic_text(&textObj, str, vx + 35, vy + 98, 0.38f, tc->amber);
        snprintf(str, sizeof(str), "%d FPS | %s", has_signal ? g_cam_fps : 0, (g_cam_zoom_mode == 1) ? "1:1 CROP" : "FIT");
        graphics_draw_dynamic_text(&textObj, str, vx + 35, vy + 118, 0.38f, tc->textDim);
    }
    else if (has_signal && s_last_frame_w > 0 && s_last_frame_h > 0)
    {
        // Render camera texture
        float scale_x = vw / (float)s_last_frame_w;
        float scale_y = vh / (float)s_last_frame_h;
        C2D_DrawImageAt(s_cam_img, vx, vy, 0.5f, NULL, scale_x, scale_y);

        // MK7 Scanline CRT overlay
        for (float ly = vy; ly < vy + vh; ly += 3.0f)
        {
            C2D_DrawRectSolid(vx, ly, 0, vw, 1, C2D_Color32(0, 0, 0, 45));
        }
    }
    else
    {
        // Standby screen
        C2D_DrawRectSolid(vx, vy, 0, vw, vh, C2D_Color32(10, 15, 20, 255));

        // Animated radar scan line
        float scan_pos = fmodf((float)(now % 2000) / 2000.0f * vh, vh);
        C2D_DrawRectSolid(vx, vy + scan_pos, 0, vw, 2, C2D_Color32(tc->amber & 0xFF, (tc->amber >> 8) & 0xFF, (tc->amber >> 16) & 0xFF, 100));

        graphics_draw_dynamic_text(&textObj, "STREAM STANDBY", vx + 52, vy + 55, 0.55f, tc->amber);
        graphics_draw_dynamic_text(&textObj, "Select source via PC Tray / Web", vx + 22, vy + 78, 0.38f, tc->textDim);
        snprintf(str, sizeof(str), "PORT: %d", g_stream_port);
        graphics_draw_dynamic_text(&textObj, str, vx + 85, vy + 98, 0.38f, tc->cyan);
    }

    // MK7 Broadcast Overlay Brackets
    draw_cam_brackets(vx + 4.0f, vy + 4.0f, vw - 8.0f, vh - 8.0f, 12.0f, has_signal ? tc->amber : tc->panelDim);

    if (has_signal)
    {
        // Pulsing LIVE dot & badge
        float pulse = (sinf((float)(now % 1000) / 1000.0f * 6.28318f) + 1.0f) * 0.5f;
        u8 dot_alpha = (u8)(140 + pulse * 115);
        u32 dot_color = C2D_Color32(235, 40, 40, dot_alpha);
        C2D_DrawCircleSolid(vx + 14.0f, vy + 14.0f, 0, 4.0f, dot_color);
        graphics_draw_dynamic_text(&textObj, "LIVE", vx + 24.0f, vy + 8.0f, 0.45f, tc->text);
    }
    else
    {
        C2D_DrawCircleSolid(vx + 14.0f, vy + 14.0f, 0, 4.0f, tc->amber);
        graphics_draw_dynamic_text(&textObj, "STANDBY", vx + 24.0f, vy + 8.0f, 0.42f, tc->textDim);
    }

    // Timecode & FPS
    time_t t = time(NULL);
    struct tm *tmv = localtime(&t);
    int ms = (int)(now % 1000) / 100;
    snprintf(str, sizeof(str), "%02d:%02d:%02d.%d", tmv->tm_hour, tmv->tm_min, tmv->tm_sec, ms);
    graphics_draw_dynamic_text(&textObj, str, vx + vw - 85.0f, vy + 8.0f, 0.40f, tc->textDim);

    // Source Name & Zoom Badge
    char src_label[48];
    if (has_signal) {
        snprintf(src_label, sizeof(src_label), "SRC: %s", g_cam_source_name[0] ? g_cam_source_name : "DISPLAY");
    } else {
        snprintf(src_label, sizeof(src_label), "SRC: NONE (STANDBY)");
    }
    graphics_draw_dynamic_text(&textObj, src_label, vx + 10.0f, vy + vh - 18.0f, 0.38f, has_signal ? tc->cyan : tc->textDim);

    const char *zoom_str = (g_cam_zoom_mode == 1) ? "1:1 CROP" : "FIT";
    snprintf(str, sizeof(str), "%s | %d FPS", zoom_str, has_signal ? g_cam_fps : 0);
    graphics_draw_dynamic_text(&textObj, str, vx + vw - 80.0f, vy + vh - 18.0f, 0.38f, tc->textDim);

    // Bottom controls helper bar
    graphics_draw_hud_panel(14.0f, 188.0f, 248.0f, 44.0f, tc->panel, tc->amber, 6.0f);
    graphics_draw_dynamic_text(&textObj, "(X) Swap Top", 24.0f, 194.0f, 0.38f, tc->text);
    graphics_draw_dynamic_text(&textObj, "(L/R) Prev/Next Source", 120.0f, 194.0f, 0.38f, tc->textDim);
    graphics_draw_dynamic_text(&textObj, "(A) Zoom  (Y) OS Picker", 24.0f, 212.0f, 0.38f, tc->cyan);
}

// ---------------------------------------------------------------------------
// Top Screen Fullscreen Widescreen Spectator Mode
// ---------------------------------------------------------------------------
void graphics_draw_top_screen_cam(void)
{
    const ThemeColors *tc = graphics_get_colors();
    C2D_Text textObj;
    char str[128];

    u64 now = osGetTime();
    int has_signal = (s_frame_count > 0 && (now - g_cam_last_frame_time < 3000));

    if (has_signal && s_last_frame_w > 0 && s_last_frame_h > 0)
    {
        float scale_x = 400.0f / (float)s_last_frame_w;
        float scale_y = 240.0f / (float)s_last_frame_h;
        C2D_DrawImageAt(s_cam_img, 0, 0, 0.5f, NULL, scale_x, scale_y);

        // Scanlines
        for (float ly = 0; ly < 240.0f; ly += 3.0f)
        {
            C2D_DrawRectSolid(0, ly, 0, 400, 1, C2D_Color32(0, 0, 0, 50));
        }
    }
    else
    {
        C2D_DrawRectSolid(0, 0, 0, 400, 240, C2D_Color32(8, 12, 18, 255));
        graphics_draw_dynamic_text(&textObj, "WAITING FOR FEED...", 130, 110, 0.6f, tc->amber);
    }

    // MK7 OSD Frame & Brackets
    draw_cam_brackets(10, 10, 380, 220, 20.0f, tc->amber);

    // Pulsing LIVE dot & badge
    float pulse = (sinf((float)(now % 1000) / 1000.0f * 6.28318f) + 1.0f) * 0.5f;
    u8 dot_alpha = (u8)(140 + pulse * 115);
    u32 dot_color = C2D_Color32(235, 40, 40, dot_alpha);
    C2D_DrawCircleSolid(22.0f, 22.0f, 0, 5.0f, dot_color);

    graphics_draw_dynamic_text(&textObj, "● SPECTATOR CAM", 34.0f, 15.0f, 0.55f, tc->text);

    // Timecode
    time_t t = time(NULL);
    struct tm *tmv = localtime(&t);
    int ms = (int)(now % 1000) / 100;
    snprintf(str, sizeof(str), "REC %02d:%02d:%02d.%d", tmv->tm_hour, tmv->tm_min, tmv->tm_sec, ms);
    graphics_draw_dynamic_text(&textObj, str, 260.0f, 15.0f, 0.45f, tc->textDim);

    const char *zoom_str = (g_cam_zoom_mode == 1) ? "1:1 CROP" : "FIT";
    snprintf(str, sizeof(str), "SRC: %s  |  %s  |  %d FPS", g_cam_source_name[0] ? g_cam_source_name : "DISPLAY", zoom_str, g_cam_fps);
    graphics_draw_dynamic_text(&textObj, str, 22.0f, 214.0f, 0.45f, tc->cyan);

    graphics_draw_dynamic_text(&textObj, "(X: Return)", 310.0f, 214.0f, 0.42f, tc->textDim);
}
#endif
