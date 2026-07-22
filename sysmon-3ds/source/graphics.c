#include "graphics.h"
#include "network.h"
#include <stdio.h>
#include <time.h>
#include <3ds/services/ptmu.h>

extern int g_screen_mode;
extern int g_pomodoro_seconds;
extern int g_pomodoro_active;
extern int g_selected_proc;

static C3D_RenderTarget* top;
static C3D_RenderTarget* bottom;
static C2D_TextBuf dynamicBuf;
static C2D_TextBuf staticBuf;
static C2D_Font customFont;
static u32 clrBg, clrPanel, clrText, clrHighlight;

// Static texts
static C2D_Text txt_cpu, txt_gpu, txt_history;
static C2D_Text txt_pomo_tab, txt_kill_tab, txt_macro_tab, txt_media_tab, txt_set_tab;
static C2D_Text txt_pomo_title, txt_pomo_help;
static C2D_Text txt_kill_title, txt_kill_help;
static C2D_Text txt_macro_title, txt_macro_help1;
static C2D_Text txt_media_title;
static C2D_Text txt_set_title;

static void parse_static_text(C2D_Text* textObj, const char* str) {
    if (customFont) {
        C2D_TextFontParse(textObj, customFont, staticBuf, str);
    } else {
        C2D_TextParse(textObj, staticBuf, str);
    }
    C2D_TextOptimize(textObj);
}

void graphics_init() {
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    dynamicBuf = C2D_TextBufNew(4096);
    staticBuf = C2D_TextBufNew(4096);

    customFont = C2D_FontLoad("romfs:/minecraft.bcfnt");

    clrBg = C2D_Color32(30, 30, 30, 255);
    clrPanel = C2D_Color32(50, 50, 50, 255);
    clrText = C2D_Color32(255, 255, 255, 255);
    clrHighlight = C2D_Color32(0, 150, 255, 255);

    // Parse all static texts once
    parse_static_text(&txt_cpu, "CPU");
    parse_static_text(&txt_gpu, "GPU");
    parse_static_text(&txt_history, "HISTORY");
    
    parse_static_text(&txt_pomo_tab, "POMO");
    parse_static_text(&txt_kill_tab, "KILL");
    parse_static_text(&txt_macro_tab, "MACRO");
    parse_static_text(&txt_media_tab, "MEDIA");
    parse_static_text(&txt_set_tab, "SET");

    parse_static_text(&txt_pomo_title, "POMODORO TIMER");
    parse_static_text(&txt_pomo_help, "A: Start/Pause   Y: Reset");

    parse_static_text(&txt_kill_title, "HANG HUNTER");
    parse_static_text(&txt_kill_help, "Tap process to kill");

    parse_static_text(&txt_macro_title, "STREAM DECK");
    parse_static_text(&txt_macro_help1, "Tap to execute macro");
    
    parse_static_text(&txt_media_title, "MEDIA CONTROLS");

    parse_static_text(&txt_set_title, "Change IP / Port");
}

void graphics_exit() {
    if (customFont) C2D_FontFree(customFont);
    C2D_TextBufDelete(dynamicBuf);
    C2D_TextBufDelete(staticBuf);
    C2D_Fini();
    C3D_Fini();
}

static u32 get_temp_color(float temp) {
    if (temp < 60.0f) return C2D_Color32(0, 255, 0, 255);
    if (temp < 80.0f) return C2D_Color32(255, 255, 0, 255);
    return C2D_Color32(255, 0, 0, 255);
}

static void draw_dynamic_text(C2D_Text* textObj, const char* str, float x, float y, float scale, u32 color) {
    if (customFont) {
        C2D_TextFontParse(textObj, customFont, dynamicBuf, str);
    } else {
        C2D_TextParse(textObj, dynamicBuf, str);
    }
    C2D_TextOptimize(textObj);
    C2D_DrawText(textObj, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

static void draw_static_text(C2D_Text* textObj, float x, float y, float scale, u32 color) {
    C2D_DrawText(textObj, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

void graphics_draw_frame(const char* ip_buffer, int port) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    
    // TOP SCREEN
    C2D_TargetClear(top, clrBg);
    C2D_SceneBegin(top);
    
    // ONLY clear dynamic buf each frame, saving massive CPU parsing time
    C2D_TextBufClear(dynamicBuf);

    if (g_cpu_temp > 85.0f) {
        C2D_DrawRectSolid(0, 0, 0, 400, 240, C2D_Color32(255, 0, 0, 100)); // Red flash
    }

    // Panels
    C2D_DrawRectSolid(10, 10, 0, 185, 90, clrPanel); // CPU
    C2D_DrawRectSolid(205, 10, 0, 185, 90, clrPanel); // GPU
    C2D_DrawRectSolid(10, 110, 0, 250, 90, clrPanel); // History
    C2D_DrawRectSolid(270, 110, 0, 120, 90, clrPanel); // Time/Weather
    
    C2D_DrawRectSolid(10, 10, 0, 4, 90, clrHighlight);
    C2D_DrawRectSolid(205, 10, 0, 4, 90, C2D_Color32(0, 255, 120, 255));
    C2D_DrawRectSolid(10, 110, 0, 4, 90, C2D_Color32(180, 0, 255, 255));
    C2D_DrawRectSolid(270, 110, 0, 4, 90, C2D_Color32(255, 255, 0, 255));

    char textStr[256];
    C2D_Text textObj;

    draw_static_text(&txt_cpu, 20, 15, 0.6f, clrText);
    draw_static_text(&txt_gpu, 215, 15, 0.6f, clrText);
    draw_static_text(&txt_history, 20, 115, 0.6f, clrText);

    // CPU Bars
    snprintf(textStr, sizeof(textStr), "Usage: %.1f%%", g_cpu_usage);
    draw_dynamic_text(&textObj, textStr, 20, 40, 0.45f, clrText);
    float usage_w = (g_cpu_usage / 100.0f) * 165.0f;
    if (usage_w > 165.0f) usage_w = 165.0f;
    C2D_DrawRectSolid(20, 52, 0, 165, 8, C2D_Color32(70,70,70,255));
    C2D_DrawRectSolid(20, 52, 0, usage_w, 8, clrHighlight);

    snprintf(textStr, sizeof(textStr), "Temp: %.1f C   Fan: %d", g_cpu_temp, g_cpu_fan);
    draw_dynamic_text(&textObj, textStr, 20, 65, 0.45f, clrText);
    float ctemp_w = (g_cpu_temp / 100.0f) * 165.0f;
    if (ctemp_w > 165.0f) ctemp_w = 165.0f;
    C2D_DrawRectSolid(20, 77, 0, 165, 8, C2D_Color32(70,70,70,255));
    C2D_DrawRectSolid(20, 77, 0, ctemp_w, 8, get_temp_color(g_cpu_temp));

    // GPU Bars
    snprintf(textStr, sizeof(textStr), "Temp: %.1f C   Fan: %d", g_gpu_temp, g_gpu_fan);
    draw_dynamic_text(&textObj, textStr, 215, 40, 0.45f, clrText); 
    float gtemp_w = (g_gpu_temp / 100.0f) * 165.0f;
    if (gtemp_w > 165.0f) gtemp_w = 165.0f;
    C2D_DrawRectSolid(215, 52, 0, 165, 8, C2D_Color32(70,70,70,255));
    C2D_DrawRectSolid(215, 52, 0, gtemp_w, 8, get_temp_color(g_gpu_temp));

    // Free RAM
    snprintf(textStr, sizeof(textStr), "Free RAM: %.2f GB", g_free_ram);
    draw_dynamic_text(&textObj, textStr, 215, 70, 0.5f, clrText);

    // History
    if (g_history_count > 1) {
        float start_x = 20;
        float start_y = 190;
        float dx = 230.0f / 9.0f;
        for (int i = 0; i < g_history_count - 1; i++) {
            float x1 = start_x + (i * dx);
            float y1 = start_y - (g_temp_history[i] / 100.0f * 40.0f);
            float x2 = start_x + ((i+1) * dx);
            float y2 = start_y - (g_temp_history[i+1] / 100.0f * 40.0f);
            C2D_DrawLine(x1, y1, clrHighlight, x2, y2, clrHighlight, 2.0f, 0);
            
            float gy1 = start_y - (g_gpu_temp_history[i] / 100.0f * 40.0f);
            float gy2 = start_y - (g_gpu_temp_history[i+1] / 100.0f * 40.0f);
            C2D_DrawLine(x1, gy1, C2D_Color32(255,0,0,255), x2, gy2, C2D_Color32(255,0,0,255), 2.0f, 0);
        }
    }

    // Time/Weather
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(textStr, sizeof(textStr), "%02d:%02d", tm->tm_hour, tm->tm_min);
    draw_dynamic_text(&textObj, textStr, 280, 125, 1.2f, clrText);
    draw_dynamic_text(&textObj, g_weather, 280, 165, 0.5f, clrText);

    // Status / Battery
    if (g_http_status == 1) {
        draw_dynamic_text(&textObj, "TCP: Connected", 10, 210, 0.5f, C2D_Color32(100, 255, 100, 255));
    } else if (g_http_status == 0) {
        draw_dynamic_text(&textObj, "TCP: Connecting...", 10, 210, 0.5f, C2D_Color32(100, 255, 255, 255));
    } else {
        draw_dynamic_text(&textObj, "TCP: Error", 10, 210, 0.5f, C2D_Color32(255, 100, 100, 255));
    }

    if (!g_fetching_enabled) {
        draw_dynamic_text(&textObj, "(Paused)", 130, 210, 0.5f, C2D_Color32(255, 255, 100, 255));
    }

    u8 battery_level = 0;
    u8 is_charging = 0;
    PTMU_GetBatteryLevel(&battery_level);
    PTMU_GetBatteryChargeState(&is_charging);
    if (is_charging) {
        snprintf(textStr, sizeof(textStr), "CHG: %d%%", battery_level * 20);
        draw_dynamic_text(&textObj, textStr, 330, 210, 0.4f, C2D_Color32(0, 255, 120, 255));
    } else {
        snprintf(textStr, sizeof(textStr), "BAT: %d%%", battery_level * 20);
        draw_dynamic_text(&textObj, textStr, 330, 210, 0.4f, battery_level <= 1 ? C2D_Color32(255, 0, 0, 255) : clrHighlight);
    }

    if (g_has_notification) {
        C2D_DrawRectSolid(0, 0, 0, 400, 30, C2D_Color32(255, 100, 0, 255));
        draw_dynamic_text(&textObj, "DESKTOP NOTIFICATION", 80, 5, 0.7f, clrText);
    }

    // BOTTOM SCREEN
    C2D_TargetClear(bottom, clrBg);
    C2D_SceneBegin(bottom);

    // Draw Tabs (Right Side)
    u32 t0 = (g_screen_mode == 0) ? clrHighlight : clrPanel;
    u32 t1 = (g_screen_mode == 1) ? clrHighlight : clrPanel;
    u32 t2 = (g_screen_mode == 2) ? clrHighlight : clrPanel;
    u32 t3 = (g_screen_mode == 3) ? clrHighlight : clrPanel;
    u32 t4 = (g_screen_mode == 4) ? clrHighlight : clrPanel;

    C2D_DrawRectSolid(270, 0, 0, 50, 48, t0);
    C2D_DrawRectSolid(270, 48, 0, 50, 48, t1);
    C2D_DrawRectSolid(270, 96, 0, 50, 48, t2);
    C2D_DrawRectSolid(270, 144, 0, 50, 48, t3);
    C2D_DrawRectSolid(270, 192, 0, 50, 48, t4);

    draw_static_text(&txt_pomo_tab, 275, 15, 0.45f, clrText);
    draw_static_text(&txt_kill_tab, 275, 63, 0.45f, clrText);
    draw_static_text(&txt_macro_tab, 275, 111, 0.45f, clrText);
    draw_static_text(&txt_media_tab, 275, 159, 0.45f, clrText);
    draw_static_text(&txt_set_tab, 275, 207, 0.45f, clrText);

    if (g_kill_confirm_pid > 0) {
        C2D_DrawRectSolid(20, 60, 0, 230, 120, C2D_Color32(20, 20, 20, 240));
        C2D_DrawRectSolid(20, 60, 0, 230, 4, C2D_Color32(255, 50, 50, 255));
        
        snprintf(textStr, sizeof(textStr), "Kill %s?", g_kill_confirm_name);
        draw_dynamic_text(&textObj, textStr, 30, 80, 0.6f, C2D_Color32(255, 100, 100, 255));
        
        draw_dynamic_text(&textObj, "Press (A) to Confirm", 50, 120, 0.5f, C2D_Color32(0, 255, 120, 255));
        draw_dynamic_text(&textObj, "Press (B) to Cancel", 50, 140, 0.5f, clrText);
    }
    else if (g_screen_mode == 0) {
        draw_static_text(&txt_pomo_title, 40, 50, 0.7f, C2D_Color32(0, 255, 120, 255));
        
        int m = g_pomodoro_seconds / 60;
        int s = g_pomodoro_seconds % 60;
        snprintf(textStr, sizeof(textStr), "%02d:%02d", m, s);
        draw_dynamic_text(&textObj, textStr, 70, 90, 1.5f, clrText);
        
        draw_static_text(&txt_pomo_help, 40, 160, 0.5f, clrText);
    }
    else if (g_screen_mode == 1) {
        draw_static_text(&txt_kill_title, 10, 10, 0.6f, C2D_Color32(255, 100, 100, 255));
        draw_static_text(&txt_kill_help, 10, 30, 0.45f, clrText);
        
        for (int i = 0; i < g_proc_count; i++) {
            u32 color = clrPanel;
            C2D_DrawRectSolid(10, 50 + (i * 35), 0, 250, 30, color);
            
            snprintf(textStr, sizeof(textStr), "[%d] %s (%.1f%%)", (int)g_top_procs[i].pid, g_top_procs[i].name, g_top_procs[i].cpu);
            draw_dynamic_text(&textObj, textStr, 15, 55 + (i * 35), 0.5f, clrText);
        }
    }
    else if (g_screen_mode == 2) {
        draw_static_text(&txt_macro_title, 10, 10, 0.6f, C2D_Color32(180, 0, 255, 255));
        draw_static_text(&txt_macro_help1, 10, 30, 0.45f, clrText);
        
        int cols = 2;
        int btn_w = 120;
        int btn_h = 40;
        int start_x = 10;
        int start_y = 60;
        
        for (int i = 0; i < g_macro_count; i++) {
            int row = i / cols;
            int col = i % cols;
            int bx = start_x + (col * (btn_w + 10));
            int by = start_y + (row * (btn_h + 10));
            
            u32 c = clrPanel;
            if (strstr(g_macros[i].color, "blue")) c = C2D_Color32(0, 100, 255, 255);
            else if (strstr(g_macros[i].color, "red")) c = C2D_Color32(200, 50, 50, 255);
            else if (strstr(g_macros[i].color, "green")) c = C2D_Color32(50, 200, 50, 255);
            else if (strstr(g_macros[i].color, "purple")) c = C2D_Color32(150, 50, 200, 255);
            
            C2D_DrawRectSolid(bx, by, 0, btn_w, btn_h, c);
            draw_dynamic_text(&textObj, g_macros[i].label, bx + 5, by + 10, 0.4f, clrText);
        }
    }
    else if (g_screen_mode == 3) {
        draw_static_text(&txt_media_title, 10, 10, 0.6f, C2D_Color32(0, 200, 255, 255));
        
        C2D_DrawRectSolid(10, 40, 0, 250, 40, clrPanel);
        if (g_now_playing[0]) {
            draw_dynamic_text(&textObj, g_now_playing, 15, 50, 0.5f, clrText);
        } else {
            draw_dynamic_text(&textObj, "No media playing", 15, 50, 0.5f, C2D_Color32(150, 150, 150, 255));
        }
        
        C2D_DrawRectSolid(20, 100, 0, 60, 40, C2D_Color32(60, 60, 60, 255));
        draw_dynamic_text(&textObj, "<<", 35, 110, 0.6f, clrText);
        
        C2D_DrawRectSolid(100, 100, 0, 70, 40, clrHighlight);
        draw_dynamic_text(&textObj, "PLAY", 115, 110, 0.6f, clrText);
        
        C2D_DrawRectSolid(190, 100, 0, 60, 40, C2D_Color32(60, 60, 60, 255));
        draw_dynamic_text(&textObj, ">>", 205, 110, 0.6f, clrText);
    }
    else if (g_screen_mode == 4) {
        C2D_DrawRectSolid(35, 80, 0, 200, 40, clrHighlight);
        draw_static_text(&txt_set_title, 65, 90, 0.6f, clrText);

        snprintf(textStr, sizeof(textStr), "Server: %s:%d", ip_buffer, port);
        draw_dynamic_text(&textObj, textStr, 35, 130, 0.5f, clrText);
    }

    C3D_FrameEnd(0);
}
