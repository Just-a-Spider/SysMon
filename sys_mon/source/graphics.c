#include "graphics.h"
#include "network.h"
#include <stdio.h>

static C3D_RenderTarget* top;
static C3D_RenderTarget* bottom;
static C2D_TextBuf dynamicBuf;
static C2D_Font customFont;
static u32 clrBg, clrPanel, clrText, clrHighlight;

void graphics_init() {
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    dynamicBuf = C2D_TextBufNew(4096);

    customFont = C2D_FontLoad("romfs:/minecraft.bcfnt");

    clrBg = C2D_Color32(30, 30, 30, 255);
    clrPanel = C2D_Color32(50, 50, 50, 255);
    clrText = C2D_Color32(255, 255, 255, 255);
    clrHighlight = C2D_Color32(0, 150, 255, 255);
}

void graphics_exit() {
    if (customFont) C2D_FontFree(customFont);
    C2D_TextBufDelete(dynamicBuf);
    C2D_Fini();
    C3D_Fini();
}

static u32 get_temp_color(float temp) {
    if (temp < 60.0f) return C2D_Color32(0, 255, 0, 255);
    if (temp < 80.0f) return C2D_Color32(255, 255, 0, 255);
    return C2D_Color32(255, 0, 0, 255);
}

static void draw_text(C2D_Text* textObj, const char* str, float x, float y, float scale, u32 color) {
    if (customFont) {
        C2D_TextFontParse(textObj, customFont, dynamicBuf, str);
    } else {
        C2D_TextParse(textObj, dynamicBuf, str);
    }
    C2D_TextOptimize(textObj);
    C2D_DrawText(textObj, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

void graphics_draw_frame(const char* ip_buffer, int port) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    
    // TOP SCREEN
    C2D_TargetClear(top, clrBg);
    C2D_SceneBegin(top);
    
    C2D_TextBufClear(dynamicBuf);

    C2D_DrawRectSolid(20, 20, 0, 170, 90, clrPanel);
    C2D_DrawRectSolid(210, 20, 0, 170, 90, clrPanel);
    C2D_DrawRectSolid(20, 130, 0, 360, 90, clrPanel);

    char textStr[256];
    C2D_Text textObj;

    draw_text(&textObj, "CPU", 25, 25, 0.6f, clrHighlight);
    draw_text(&textObj, "GPU", 215, 25, 0.6f, clrHighlight);
    draw_text(&textObj, "SYSTEM MEMORY & HISTORY", 25, 135, 0.6f, clrHighlight);

    // CPU Bars
    float usage_w = (g_cpu_usage / 100.0f) * 120.0f;
    if (usage_w > 120.0f) usage_w = 120.0f;
    C2D_DrawRectSolid(25, 50, 0, 120, 8, C2D_Color32(70,70,70,255));
    C2D_DrawRectSolid(25, 50, 0, usage_w, 8, clrHighlight);
    snprintf(textStr, sizeof(textStr), "Usage: %.1f%%", g_cpu_usage);
    draw_text(&textObj, textStr, 150, 48, 0.45f, clrText);

    float ctemp_w = (g_cpu_temp / 100.0f) * 120.0f;
    if (ctemp_w > 120.0f) ctemp_w = 120.0f;
    C2D_DrawRectSolid(25, 65, 0, 120, 8, C2D_Color32(70,70,70,255));
    C2D_DrawRectSolid(25, 65, 0, ctemp_w, 8, get_temp_color(g_cpu_temp));
    snprintf(textStr, sizeof(textStr), "Temp: %.1f C", g_cpu_temp);
    draw_text(&textObj, textStr, 150, 63, 0.45f, clrText);
    
    snprintf(textStr, sizeof(textStr), "Fan: %d RPM", g_cpu_fan);
    draw_text(&textObj, textStr, 25, 80, 0.5f, clrText);

    // GPU Bars
    float gtemp_w = (g_gpu_temp / 100.0f) * 120.0f;
    if (gtemp_w > 120.0f) gtemp_w = 120.0f;
    C2D_DrawRectSolid(215, 65, 0, 120, 8, C2D_Color32(70,70,70,255));
    C2D_DrawRectSolid(215, 65, 0, gtemp_w, 8, get_temp_color(g_gpu_temp));
    snprintf(textStr, sizeof(textStr), "Temp: %.1f C", g_gpu_temp);
    draw_text(&textObj, textStr, 215, 50, 0.45f, clrText); 
    
    snprintf(textStr, sizeof(textStr), "Fan: %d RPM", g_gpu_fan);
    draw_text(&textObj, textStr, 215, 80, 0.5f, clrText);

    // Memory & History
    snprintf(textStr, sizeof(textStr), "Free RAM: %.2f GB   Status: %d", g_free_ram, g_http_status);
    draw_text(&textObj, textStr, 25, 160, 0.5f, clrText);

    if (g_history_count > 1) {
        float start_x = 215;
        float start_y = 205;
        float dx = 140.0f / 9.0f;
        for (int i = 0; i < g_history_count - 1; i++) {
            float x1 = start_x + (i * dx);
            float y1 = start_y - (g_temp_history[i] / 100.0f * 40.0f);
            float x2 = start_x + ((i+1) * dx);
            float y2 = start_y - (g_temp_history[i+1] / 100.0f * 40.0f);
            C2D_DrawLine(x1, y1, clrHighlight, x2, y2, clrHighlight, 2.0f, 0);
        }
    }

    // BOTTOM SCREEN
    C2D_TargetClear(bottom, clrBg);
    C2D_SceneBegin(bottom);

    C2D_DrawRectSolid(60, 100, 0, 200, 40, clrHighlight);
    draw_text(&textObj, "Change IP / Port", 90, 110, 0.6f, clrText);

    snprintf(textStr, sizeof(textStr), "Server: %s:%d", ip_buffer, port);
    draw_text(&textObj, textStr, 60, 150, 0.5f, clrText);

    C3D_FrameEnd(0);
}
