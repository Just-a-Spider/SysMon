#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <3ds.h>
#include <3ds/services/ptmu.h>

#include "network.h"
#include "graphics.h"
#include "input.h"

int g_screen_mode = 0;
int g_tab_scroll = 0;
int g_control_target = 0;
int g_volume_level = 72;
int g_brightness_level = 48;
int g_pomodoro_preset_index = 1;
int g_pomodoro_custom_minutes = 25;
int g_pomodoro_seconds = 25 * 60;
int g_pomodoro_active = 0;
int g_selected_proc = 0;

u64 g_last_level_touch_time = 0;
static u64 g_last_level_send_time = 0;
static u64 last_btn_time = 0;

static const int pomo_preset_minutes[] = {15, 25, 45, 60};
static const int pomo_preset_count = 5;

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static int current_pomodoro_minutes(void)
{
    if (g_pomodoro_preset_index >= 0 && g_pomodoro_preset_index < 4)
        return pomo_preset_minutes[g_pomodoro_preset_index];
    return clamp_int(g_pomodoro_custom_minutes, 5, 180);
}

static void save_pomodoro_config(void)
{
    FILE *f_out = fopen("sdmc:/sysmon_pomo.txt", "w");
    if (f_out)
    {
        fprintf(f_out, "%d %d", g_pomodoro_preset_index, g_pomodoro_custom_minutes);
        fclose(f_out);
    }
}

static void load_pomodoro_config(void)
{
    FILE *f = fopen("sdmc:/sysmon_pomo.txt", "r");
    if (f)
    {
        if (fscanf(f, "%d %d", &g_pomodoro_preset_index, &g_pomodoro_custom_minutes) < 2)
        {
            g_pomodoro_preset_index = 1;
            g_pomodoro_custom_minutes = 25;
        }
        fclose(f);
    }

    g_pomodoro_preset_index = clamp_int(g_pomodoro_preset_index, 0, pomo_preset_count - 1);
    g_pomodoro_custom_minutes = clamp_int(g_pomodoro_custom_minutes, 5, 180);
}

static void sync_pomodoro_seconds(void)
{
    if (!g_pomodoro_active)
        g_pomodoro_seconds = current_pomodoro_minutes() * 60;
}

static void set_pomodoro_preset(int delta)
{
    g_pomodoro_preset_index = (g_pomodoro_preset_index + delta + pomo_preset_count) % pomo_preset_count;
    save_pomodoro_config();
    sync_pomodoro_seconds();
}

static void adjust_custom_pomodoro_minutes(int delta)
{
    if (g_pomodoro_preset_index != 4)
        return;

    g_pomodoro_custom_minutes = clamp_int(g_pomodoro_custom_minutes + delta, 5, 180);
    save_pomodoro_config();
    sync_pomodoro_seconds();
}

static int throttle_level_from_touch(float px, float py)
{
    const float cx = 132.0f;
    const float cy = 146.0f;
    const float inner_r = 36.0f;
    const float outer_r = 70.0f;
    const float start_angle = 2.3561945f;
    const float sweep = 4.7123890f;
    const float two_pi = 6.2831853f;

    float dx = px - cx;
    float dy = cy - py;
    float dist = sqrtf((dx * dx) + (dy * dy));
    if (dist < inner_r || dist > outer_r)
        return -1;

    float angle = atan2f(dy, dx);
    if (angle < 0.0f)
        angle += two_pi;

    float pos = angle - start_angle;
    if (pos < 0.0f)
        pos += two_pi;

    if (pos > sweep)
        return -1;

    return (int)((pos / sweep) * 100.0f + 0.5f);
}

int main()
{
    gfxInitDefault();
    romfsInit();
    ptmuInit();

    char ip_buffer[60] = "192.168.0.6";
    int port = 7341;

    load_pomodoro_config();
    g_pomodoro_seconds = current_pomodoro_minutes() * 60;

    int need_prompt = 1;
    FILE *f = fopen("sdmc:/sysmon_cfg.txt", "r");
    if (f)
    {
        if (fscanf(f, "%59s %d %15s", ip_buffer, &port, g_auth_key) >= 2)
        {
            need_prompt = 0;
        }
        fclose(f);
    }

    if (need_prompt)
    {
        prompt_for_ip(ip_buffer, sizeof(ip_buffer));
        prompt_for_port(&port);
        prompt_for_key(g_auth_key, sizeof(g_auth_key));

        FILE *f_out = fopen("sdmc:/sysmon_cfg.txt", "w");
        if (f_out)
        {
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
        u32 kUp = hidKeysUp();
        if (kDown & KEY_START)
            break;

        touchPosition touch;
        hidTouchRead(&touch);
        u32 kHeld = hidKeysHeld();

        // Tab drawer: every touch is a discrete tap (row or arrow) - no
        // drag gestures, since the resistive touchscreen handles taps far
        // more reliably than swipes.
        if (g_kill_confirm_pid == 0 && (kDown & KEY_TOUCH) && touch.px > 270)
        {
            int new_scroll = g_tab_scroll;
            int hit = graphics_tab_touch_hit((float)touch.px, (float)touch.py, g_tab_scroll, &new_scroll);
            if (hit == -2)
                g_tab_scroll = new_scroll;
            else if (hit >= 0)
                g_screen_mode = hit;
        }

        // Global Select to toggle fetching
        if (kDown & KEY_SELECT)
        {
            g_fetching_enabled = !g_fetching_enabled;
        }

        u64 now = osGetTime();

        if (g_kill_confirm_pid > 0)
        {
            if (kDown & KEY_A)
            {
                char msg[128];
                snprintf(msg, sizeof(msg), "{\"action\":\"kill\",\"pid\":%d,\"pin\":\"%s\"}", (int)g_kill_confirm_pid, g_auth_key);
                network_send_json(msg);
                g_kill_confirm_pid = 0;
                g_kill_confirm_name[0] = '\0';
            }
            if (kDown & KEY_B)
            {
                g_kill_confirm_pid = 0;
                g_kill_confirm_name[0] = '\0';
            }
        }
        else if (g_screen_mode == 0)
        {
            if (kDown & KEY_A)
                g_pomodoro_active = !g_pomodoro_active;
            if (kDown & KEY_Y)
            {
                g_pomodoro_seconds = current_pomodoro_minutes() * 60;
                g_pomodoro_active = 0;
            }
            if (kDown & KEY_L)
                set_pomodoro_preset(-1);
            if (kDown & KEY_R)
                set_pomodoro_preset(1);
            if (kDown & KEY_DUP)
                adjust_custom_pomodoro_minutes(5);
            if (kDown & KEY_DDOWN)
                adjust_custom_pomodoro_minutes(-5);
        }
        else if (g_screen_mode == 1)
        {
            if (kDown & KEY_TOUCH && touch.px < 260)
            {
                int index = (touch.py - 50) / 35;
                if (index >= 0 && index < g_proc_count)
                {
                    g_kill_confirm_pid = g_top_procs[index].pid;
                    strncpy(g_kill_confirm_name, g_top_procs[index].name, 31);
                }
            }
        }
        else if (g_screen_mode == 2)
        {
            if (kDown && (now - last_btn_time > 300))
            {
                char *btn = NULL;

                if (kDown & KEY_TOUCH && touch.px < 260)
                {
                    int cols = 2;
                    int btn_w = 120;
                    int btn_h = 40;
                    int start_x = 10;
                    int start_y = 60;

                    for (int i = 0; i < g_macro_count; i++)
                    {
                        int row = i / cols;
                        int col = i % cols;
                        int bx = start_x + (col * (btn_w + 10));
                        int by = start_y + (row * (btn_h + 10));

                        if (touch.px >= bx && touch.px <= bx + btn_w &&
                            touch.py >= by && touch.py <= by + btn_h)
                        {
                            btn = g_macros[i].button;
                            break;
                        }
                    }
                }
                else
                {
                    if (kDown & KEY_A)
                        btn = "A";
                    else if (kDown & KEY_B)
                        btn = "B";
                    else if (kDown & KEY_X)
                        btn = "X";
                    else if (kDown & KEY_Y)
                        btn = "Y";
                    else if (kDown & KEY_L)
                        btn = "L";
                    else if (kDown & KEY_R)
                        btn = "R";
                    else if (kDown & KEY_ZL)
                        btn = "ZL";
                    else if (kDown & KEY_ZR)
                        btn = "ZR";
                    else if (kDown & KEY_DUP)
                        btn = "DUP";
                    else if (kDown & KEY_DDOWN)
                        btn = "DDOWN";
                    else if (kDown & KEY_DLEFT)
                        btn = "DLEFT";
                    else if (kDown & KEY_DRIGHT)
                        btn = "DRIGHT";
                }

                if (btn)
                {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "{\"action\":\"button\",\"btn\":\"%s\",\"pin\":\"%s\"}", btn, g_auth_key);
                    network_send_json(msg);
                    last_btn_time = now;
                }
            }
        }
        else if (g_screen_mode == 3)
        {
            if (kDown & KEY_TOUCH && touch.py >= 100 && touch.py <= 140)
            {
                char *btn = NULL;
                if (touch.px >= 20 && touch.px <= 80)
                    btn = "prev";
                else if (touch.px >= 100 && touch.px <= 170)
                    btn = "playpause";
                else if (touch.px >= 190 && touch.px <= 250)
                    btn = "next";

                if (btn)
                {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "{\"action\":\"media\",\"btn\":\"%s\",\"pin\":\"%s\"}", btn, g_auth_key);
                    network_send_json(msg);
                }
            }
        }
        else if (g_screen_mode == 4)
        {
            if (kDown & KEY_TOUCH && touch.py > 80 && touch.py < 120 && touch.px > 35 && touch.px < 235)
            {
                prompt_for_ip(ip_buffer, sizeof(ip_buffer));
                prompt_for_port(&port);
                prompt_for_key(g_auth_key, sizeof(g_auth_key));

                FILE *f_out = fopen("sdmc:/sysmon_cfg.txt", "w");
                if (f_out)
                {
                    fprintf(f_out, "%s %d %s", ip_buffer, port, g_auth_key);
                    fclose(f_out);
                }

                network_exit();
                network_init(ip_buffer, port);
            }
        }
        else if (g_screen_mode == 5)
        {
            if (kDown & KEY_X)
            {
                g_control_target = !g_control_target;
            }

            if (kHeld & KEY_TOUCH)
            {
                int level = throttle_level_from_touch((float)touch.px, (float)touch.py);
                if (level >= 0)
                {
                    if (g_control_target == 0)
                    {
                        if (g_volume_level != level)
                        {
                            g_volume_level = level;
                            if (now - g_last_level_send_time > 200)
                            {
                                network_send_level("volume", level);
                                g_last_level_send_time = now;
                            }
                            g_last_level_touch_time = now;
                        }
                    }
                    else if (g_brightness_level != level)
                    {
                        g_brightness_level = level;
                        if (now - g_last_level_send_time > 200)
                        {
                            network_send_level("brightness", level);
                            g_last_level_send_time = now;
                        }
                        g_last_level_touch_time = now;
                    }
                }
            }
            if (kUp & KEY_TOUCH)
            {
                int level = throttle_level_from_touch((float)touch.px, (float)touch.py);
                if (level >= 0)
                {
                    if (g_control_target == 0)
                        network_send_level("volume", level);
                    else
                        network_send_level("brightness", level);
                    g_last_level_send_time = now;
                    g_last_level_touch_time = now;
                }
            }
        }

        static time_t lastPomoTime = 0;
        time_t currentTime = time(NULL);
        if (lastPomoTime == 0)
            lastPomoTime = currentTime;

        if (g_pomodoro_active)
        {
            if (difftime(currentTime, lastPomoTime) >= 1.0)
            {
                g_pomodoro_seconds--;
                if (g_pomodoro_seconds <= 0)
                {
                    g_pomodoro_active = 0;
                    g_pomodoro_seconds = current_pomodoro_minutes() * 60;

                    char msg[128];
                    snprintf(msg, sizeof(msg), "{\"action\":\"notify\",\"pin\":\"%s\"}", g_auth_key);
                    network_send_json(msg);
                }
                lastPomoTime = currentTime;
            }
        }
        else
        {
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