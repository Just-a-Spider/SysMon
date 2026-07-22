#include <string.h>
#include <stdio.h>
#include <time.h>
#include <3ds.h>
#include <3ds/services/ptmu.h>

#include "network.h"
#include "graphics.h"
#include "input.h"

int g_screen_mode = 0;
int g_pomodoro_seconds = 25 * 60;
int g_pomodoro_active = 0;
int g_selected_proc = 0;

static u64 last_btn_time = 0;

int main()
{
    gfxInitDefault();
    romfsInit();
    ptmuInit();

    char ip_buffer[60] = "192.168.0.6";
    int port = 7341;
    
    int need_prompt = 1;
    FILE* f = fopen("sdmc:/sysmon_cfg.txt", "r");
    if (f) {
        if (fscanf(f, "%59s %d %15s", ip_buffer, &port, g_auth_key) >= 2) {
            need_prompt = 0;
        }
        fclose(f);
    }

    if (need_prompt) {
        prompt_for_ip(ip_buffer, sizeof(ip_buffer));
        prompt_for_port(&port);
        prompt_for_key(g_auth_key, sizeof(g_auth_key));
        
        FILE* f_out = fopen("sdmc:/sysmon_cfg.txt", "w");
        if (f_out) {
            fprintf(f_out, "%s %d %s", ip_buffer, port, g_auth_key);
            fclose(f_out);
        }
    }

    network_init(ip_buffer, port);
    graphics_init();

    while (aptMainLoop())
    {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) break;

        touchPosition touch;
        hidTouchRead(&touch);

        // Touch tabs (Vertical on right side: px > 270)
        if (g_kill_confirm_pid == 0 && (kDown & KEY_TOUCH) && touch.px > 270) {
            if (touch.py < 48) g_screen_mode = 0;
            else if (touch.py < 96) g_screen_mode = 1;
            else if (touch.py < 144) g_screen_mode = 2;
            else if (touch.py < 192) g_screen_mode = 3;
            else g_screen_mode = 4;
        }

        // Global Select to toggle fetching
        if (kDown & KEY_SELECT) {
            g_fetching_enabled = !g_fetching_enabled;
        }

        u64 now = osGetTime();

        if (g_kill_confirm_pid > 0) {
            if (kDown & KEY_A) {
                char msg[128];
                snprintf(msg, sizeof(msg), "{\"action\":\"kill\",\"pid\":%d,\"pin\":\"%s\"}", (int)g_kill_confirm_pid, g_auth_key);
                network_send_json(msg);
                g_kill_confirm_pid = 0;
                g_kill_confirm_name[0] = '\0';
            }
            if (kDown & KEY_B) {
                g_kill_confirm_pid = 0;
                g_kill_confirm_name[0] = '\0';
            }
        }
        else if (g_screen_mode == 0) {
            if (kDown & KEY_A) g_pomodoro_active = !g_pomodoro_active;
            if (kDown & KEY_Y) { g_pomodoro_seconds = 25 * 60; g_pomodoro_active = 0; }
        }
        else if (g_screen_mode == 1) {
            if (kDown & KEY_TOUCH && touch.px < 260) {
                int index = (touch.py - 50) / 35;
                if (index >= 0 && index < g_proc_count) {
                    g_kill_confirm_pid = g_top_procs[index].pid;
                    strncpy(g_kill_confirm_name, g_top_procs[index].name, 31);
                }
            }
        }
        else if (g_screen_mode == 2) {
            if (kDown && (now - last_btn_time > 300)) {
                char* btn = NULL;
                
                if (kDown & KEY_TOUCH && touch.px < 260) {
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
                        
                        if (touch.px >= bx && touch.px <= bx + btn_w &&
                            touch.py >= by && touch.py <= by + btn_h) {
                            btn = g_macros[i].button;
                            break;
                        }
                    }
                }
                else {
                    if (kDown & KEY_A) btn = "A";
                    else if (kDown & KEY_B) btn = "B";
                    else if (kDown & KEY_X) btn = "X";
                    else if (kDown & KEY_Y) btn = "Y";
                    else if (kDown & KEY_L) btn = "L";
                    else if (kDown & KEY_R) btn = "R";
                    else if (kDown & KEY_ZL) btn = "ZL";
                    else if (kDown & KEY_ZR) btn = "ZR";
                    else if (kDown & KEY_DUP) btn = "DUP";
                    else if (kDown & KEY_DDOWN) btn = "DDOWN";
                    else if (kDown & KEY_DLEFT) btn = "DLEFT";
                    else if (kDown & KEY_DRIGHT) btn = "DRIGHT";
                }
                
                if (btn) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "{\"action\":\"button\",\"btn\":\"%s\",\"pin\":\"%s\"}", btn, g_auth_key);
                    network_send_json(msg);
                    last_btn_time = now;
                }
            }
        }
        else if (g_screen_mode == 3) {
            if (kDown & KEY_TOUCH && touch.py >= 100 && touch.py <= 140) {
                char* btn = NULL;
                if (touch.px >= 20 && touch.px <= 80) btn = "prev";
                else if (touch.px >= 100 && touch.px <= 170) btn = "playpause";
                else if (touch.px >= 190 && touch.px <= 250) btn = "next";
                
                if (btn) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "{\"action\":\"media\",\"btn\":\"%s\",\"pin\":\"%s\"}", btn, g_auth_key);
                    network_send_json(msg);
                }
            }
        }
        else if (g_screen_mode == 4) {
            if (kDown & KEY_TOUCH && touch.py > 80 && touch.py < 120 && touch.px > 35 && touch.px < 235) {
                prompt_for_ip(ip_buffer, sizeof(ip_buffer));
                prompt_for_port(&port);
                prompt_for_key(g_auth_key, sizeof(g_auth_key));
                
                FILE* f_out = fopen("sdmc:/sysmon_cfg.txt", "w");
                if (f_out) {
                    fprintf(f_out, "%s %d %s", ip_buffer, port, g_auth_key);
                    fclose(f_out);
                }
                
                network_exit();
                network_init(ip_buffer, port);
            }
        }

        static time_t lastPomoTime = 0;
        time_t currentTime = time(NULL);
        if (lastPomoTime == 0) lastPomoTime = currentTime;
        
        if (g_pomodoro_active) {
            if (difftime(currentTime, lastPomoTime) >= 1.0) {
                g_pomodoro_seconds--;
                if (g_pomodoro_seconds <= 0) {
                    g_pomodoro_active = 0;
                    g_pomodoro_seconds = 25 * 60;
                    
                    char msg[128];
                    snprintf(msg, sizeof(msg), "{\"action\":\"notify\",\"pin\":\"%s\"}", g_auth_key);
                    network_send_json(msg);
                }
                lastPomoTime = currentTime;
            }
        } else {
            lastPomoTime = currentTime;
        }

        graphics_draw_frame(ip_buffer, port);
    }

    graphics_exit();
    ptmuExit();
    network_exit();
    romfsExit();
    gfxExit();
    return 0;
}
