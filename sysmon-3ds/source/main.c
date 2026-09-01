#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <3ds.h>
#include <3ds/services/ptmu.h>
#include <3ds/services/irrst.h>

#include "network.h"
#include "net_ctrl.h"
#include "graphics.h"
#include "input.h"
#include "audio.h"

// ---------------------------------------------------------------------------
// Global UI state
// ---------------------------------------------------------------------------
int g_screen_mode        = 0;
int g_tab_scroll         = 0;
int g_control_target     = 0;
int g_volume_level       = 72;
int g_brightness_level   = 48;
int g_pomodoro_preset_index   = 1;
int g_pomodoro_custom_minutes = 25;
int g_pomodoro_seconds        = 25 * 60;
int g_pomodoro_active         = 0;
int g_selected_proc      = 0;

// Controller state
u32 g_ctrl_held_keys = 0;
s16 g_ctrl_cx = 0, g_ctrl_cy = 0;
s16 g_ctrl_rx = 0, g_ctrl_ry = 0;
float g_ctrl_emergency_progress = 0.0f;
int g_ctrl_touch_active = 0;
float g_ctrl_touch_rx = 220.0f, g_ctrl_touch_ry = 130.0f;
static u64 s_emergency_hold_start = 0;
static bool s_is_n3ds = false;
static bool s_irrst_inited = false;

// Frame throttle (~20 fps)
static u64 g_last_draw_time = 0;
#define FRAME_INTERVAL_MS 50

static u64 g_last_level_send_time = 0;
static u64 last_btn_time          = 0;

// ---------------------------------------------------------------------------
// SET tab state machine
// ---------------------------------------------------------------------------
static int g_set_row      = 0;   // 0=THEME  1=SERVER
static int g_set_sub      = 0;   // 0=main  1=manager  2=editor
static int g_preview_idx  = 0;   // browsed index for focused row (Left/Right)
static int g_editing      = 0;   // entry open in editor
static int g_edit_field   = 0;   // focused field in editor
static int g_edit_swatch  = 0;   // highlighted swatch in theme editor

// Backup for cancel-revert in theme editor
static ThemePreset g_edit_backup;

// ---------------------------------------------------------------------------
// Pomodoro helpers
// ---------------------------------------------------------------------------
static const int pomo_preset_minutes[] = {15, 25, 45, 60};
static const int pomo_preset_count     = 5;

static int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int current_pomodoro_minutes(void)
{
    if (g_pomodoro_preset_index >= 0 && g_pomodoro_preset_index < 4)
        return pomo_preset_minutes[g_pomodoro_preset_index];
    return clamp_int(g_pomodoro_custom_minutes, 5, 180);
}

static void save_pomodoro_config(void)
{
    FILE *f = fopen("sdmc:/sysmon_pomo.txt", "w");
    if (f) { fprintf(f, "%d %d", g_pomodoro_preset_index, g_pomodoro_custom_minutes); fclose(f); }
}

static void load_pomodoro_config(void)
{
    FILE *f = fopen("sdmc:/sysmon_pomo.txt", "r");
    if (f)
    {
        if (fscanf(f, "%d %d", &g_pomodoro_preset_index, &g_pomodoro_custom_minutes) < 2)
        {
            g_pomodoro_preset_index   = 1;
            g_pomodoro_custom_minutes = 25;
        }
        fclose(f);
    }
    g_pomodoro_preset_index   = clamp_int(g_pomodoro_preset_index,   0, pomo_preset_count - 1);
    g_pomodoro_custom_minutes = clamp_int(g_pomodoro_custom_minutes, 5, 180);
}

static void sync_pomodoro_seconds(void) { if (!g_pomodoro_active) g_pomodoro_seconds = current_pomodoro_minutes() * 60; }
static void set_pomodoro_preset(int delta)
{
    g_pomodoro_preset_index = (g_pomodoro_preset_index + delta + pomo_preset_count) % pomo_preset_count;
    save_pomodoro_config(); sync_pomodoro_seconds();
}
static void adjust_custom_pomodoro_minutes(int delta)
{
    if (g_pomodoro_preset_index != 4) return;
    g_pomodoro_custom_minutes = clamp_int(g_pomodoro_custom_minutes + delta, 5, 180);
    save_pomodoro_config(); sync_pomodoro_seconds();
}

// ---------------------------------------------------------------------------
// Level dial helper
// ---------------------------------------------------------------------------
static int throttle_level_from_touch(float px, float py)
{
    const float cx=132.0f, cy=146.0f, inner_r=36.0f, outer_r=70.0f;
    const float start_angle=2.3561945f, sweep=4.7123890f, two_pi=6.2831853f;
    float dx=px-cx, dy=cy-py;
    float dist=sqrtf(dx*dx+dy*dy);
    if (dist<inner_r||dist>outer_r) return -1;
    float angle=atan2f(dy,dx);
    if (angle<0.0f) angle+=two_pi;
    float pos=angle-start_angle;
    if (pos<0.0f) pos+=two_pi;
    if (pos>sweep) return -1;
    return (int)((pos/sweep)*100.0f+0.5f);
}

// ---------------------------------------------------------------------------
// SET sub-view helpers
// ---------------------------------------------------------------------------
static void set_enter_manager(void)
{
    g_set_sub    = 1;
    g_preview_idx = (g_set_row == 0) ? g_theme_index : g_profile_index;
    audio_play_click();
}

static void set_enter_editor(int idx)
{
    g_editing    = idx;
    g_edit_field = 0;
    g_edit_swatch = 0;
    g_set_sub    = 2;
    if (g_set_row == 0)
    {
        ThemePreset *t = graphics_get_theme_mut(idx);
        if (t) g_edit_backup = *t; // snapshot for cancel
    }
    audio_play_click();
}

static void set_apply_theme_preview(int idx)
{
    // Apply for live preview (not saved yet)
    graphics_apply_theme(idx);
}

static void set_apply_and_save_theme(int idx)
{
    g_theme_index = idx;
    graphics_apply_theme(g_theme_index);
    graphics_save_themes();
    audio_play_confirm();
}

static void set_apply_and_save_server(int idx)
{
    network_switch_profile(idx);
    audio_play_confirm();
}

static void set_revert_preview(void)
{
    // Called when leaving SET without applying — restore saved theme
    graphics_apply_theme(g_theme_index);
    g_preview_idx = g_theme_index;
    g_set_sub     = 0;
}

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------
int main(void)
{
    osSetSpeedupEnable(true); // Unlock 804MHz CPU + L2 Cache on New 3DS
    gfxInitDefault();
    romfsInit();
    ptmuInit();

    APT_CheckNew3DS(&s_is_n3ds);
    if (s_is_n3ds)
    {
        Result r = irrstInit();
        if (R_SUCCEEDED(r))
            s_irrst_inited = true;
    }

    load_pomodoro_config();
    g_pomodoro_seconds = current_pomodoro_minutes() * 60;

    // Load server profiles (migrates old sysmon_cfg.txt automatically)
    network_load_profiles();

    // Prompt only if no valid profile was loaded
    if (g_profile_count == 0 || g_profiles[0].ip[0] == '\0')
    {
        g_profile_count = 1;
        snprintf(g_profiles[0].name, sizeof(g_profiles[0].name), "HOME");
        snprintf(g_profiles[0].ip,   sizeof(g_profiles[0].ip),   "192.168.0.1");
        g_profiles[0].port = 7341;
        snprintf(g_profiles[0].pin,  sizeof(g_profiles[0].pin),  "1234");
        prompt_for_ip(g_profiles[0].ip, sizeof(g_profiles[0].ip));
        prompt_for_port(&g_profiles[0].port);
        prompt_for_key(g_profiles[0].pin, sizeof(g_profiles[0].pin));
        snprintf(g_auth_key, sizeof(g_auth_key), "%s", g_profiles[0].pin);
        g_profile_index = 0;
        network_save_profiles();
    }

    // graphics_init() calls graphics_load_themes() + graphics_apply_theme()
    network_init(g_profiles[g_profile_index].ip, g_profiles[g_profile_index].port);
    graphics_init();
    audio_init();

    g_preview_idx = g_theme_index;

    // ---------------------------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------------------------
    while (aptMainLoop())
    {
        hidScanInput();
        if (s_irrst_inited)
            irrstScanInput();

        u32 kDown = hidKeysDown();
        u32 kUp   = hidKeysUp();
        if ((kDown & KEY_START) && g_screen_mode != 7) break;

        touchPosition touch;
        hidTouchRead(&touch);
        u32 kHeld = hidKeysHeld();

        // Tab drawer — discrete taps only
        if (g_kill_confirm_pid == 0 && (kDown & KEY_TOUCH) && touch.px > 270)
        {
            int new_scroll = g_tab_scroll;
            int hit = graphics_tab_touch_hit((float)touch.px, (float)touch.py,
                                             g_tab_scroll, &new_scroll);
            if (hit == -2)
            {
                g_tab_scroll = new_scroll;
            }
            else if (hit >= 0 && hit != g_screen_mode)
            {
                // Leaving SET: revert live theme preview if not applied
                if (g_screen_mode == 4) set_revert_preview();
#ifndef DISABLE_CAM
                if (g_screen_mode == 6 && !g_cam_top_screen) network_cam_stop();
#endif
                if (g_screen_mode == 7) net_ctrl_stop();
                g_screen_mode = hit;
#ifndef DISABLE_CAM
                if (g_screen_mode == 6) { network_cam_start(); network_cam_send_cmd('B'); }
#endif
                if (g_screen_mode == 7) { net_ctrl_start(g_profiles[g_profile_index].ip, g_ctrl_port); }
                audio_play_click();
            }
        }

        // SELECT toggles fetching
        if (kDown & KEY_SELECT)
            g_fetching_enabled = !g_fetching_enabled;

        u64 now = osGetTime();

        // ----------------------------------------------------------------
        // Per-mode input
        // ----------------------------------------------------------------
        if (g_kill_confirm_pid > 0)
        {
            if (kDown & KEY_A)
            {
                char msg[128];
                snprintf(msg, sizeof(msg), "{\"action\":\"kill\",\"pid\":%d,\"pin\":\"%s\"}",
                         (int)g_kill_confirm_pid, g_auth_key);
                network_send_json(msg);
                audio_play_confirm();
                g_kill_confirm_pid = 0;
                g_kill_confirm_name[0] = '\0';
            }
            if (kDown & KEY_B)
            {
                g_kill_confirm_pid = 0;
                g_kill_confirm_name[0] = '\0';
            }
        }
        // ---- POMO (mode 0) ----
        else if (g_screen_mode == 0)
        {
            if (kDown & KEY_A)  g_pomodoro_active = !g_pomodoro_active;
            if (kDown & KEY_Y)  { g_pomodoro_seconds = current_pomodoro_minutes()*60; g_pomodoro_active=0; }
            if (kDown & KEY_L)  set_pomodoro_preset(-1);
            if (kDown & KEY_R)  set_pomodoro_preset(1);
            if (kDown & KEY_DUP)   adjust_custom_pomodoro_minutes(5);
            if (kDown & KEY_DDOWN) adjust_custom_pomodoro_minutes(-5);
        }
        // ---- KILL (mode 1) ----
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
        // ---- MACRO (mode 2) ----
        else if (g_screen_mode == 2)
        {
            if (kDown && (now - last_btn_time > 300))
            {
                char *btn = NULL;

                if (kDown & KEY_TOUCH && touch.px < 260)
                {
                    // Auto-fit grid — must match macro_tab.c constants
                    #define MACRO_COLS     2
                    #define MACRO_AREA_TOP 50
                    #define MACRO_AREA_H   188
                    #define MACRO_GAP      6
                    #define MACRO_BTN_W    ((260 - MACRO_AREA_TOP/10 - MACRO_GAP) / MACRO_COLS)

                    int rows  = (g_macro_count + MACRO_COLS - 1) / MACRO_COLS;
                    int btn_h = (MACRO_AREA_H - MACRO_GAP * (rows - 1)) / rows;
                    int row_h = btn_h + MACRO_GAP;

                    for (int i = 0; i < g_macro_count; i++)
                    {
                        int row = i / MACRO_COLS;
                        int col = i % MACRO_COLS;
                        int bx  = 10 + col * (MACRO_BTN_W + MACRO_GAP);
                        int by  = MACRO_AREA_TOP + row * row_h;

                        if (touch.px >= bx && touch.px <= bx + MACRO_BTN_W &&
                            touch.py >= by && touch.py <= by + btn_h)
                        {
                            btn = g_macros[i].button;
                            break;
                        }
                    }
                    #undef MACRO_COLS
                    #undef MACRO_AREA_TOP
                    #undef MACRO_AREA_H
                    #undef MACRO_GAP
                    #undef MACRO_BTN_W
                }
                else
                {
                    if      (kDown & KEY_A)      btn = "A";
                    else if (kDown & KEY_B)      btn = "B";
                    else if (kDown & KEY_X)      btn = "X";
                    else if (kDown & KEY_Y)      btn = "Y";
                    else if (kDown & KEY_L)      btn = "L";
                    else if (kDown & KEY_R)      btn = "R";
                    else if (kDown & KEY_ZL)     btn = "ZL";
                    else if (kDown & KEY_ZR)     btn = "ZR";
                    else if (kDown & KEY_DUP)    btn = "DUP";
                    else if (kDown & KEY_DDOWN)  btn = "DDOWN";
                    else if (kDown & KEY_DLEFT)  btn = "DLEFT";
                    else if (kDown & KEY_DRIGHT) btn = "DRIGHT";
                }

                if (btn)
                {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "{\"action\":\"button\",\"btn\":\"%s\",\"pin\":\"%s\"}",
                             btn, g_auth_key);
                    network_send_json(msg);
                    audio_play_confirm();
                    last_btn_time = now;
                }
            }
        }
        // ---- MEDIA (mode 3) ----
        else if (g_screen_mode == 3)
        {
            if (kDown & KEY_TOUCH && touch.py >= 100 && touch.py <= 140)
            {
                char *btn = NULL;
                if      (touch.px >= 20  && touch.px <= 80)  btn = "prev";
                else if (touch.px >= 100 && touch.px <= 170) btn = "playpause";
                else if (touch.px >= 190 && touch.px <= 250) btn = "next";

                if (btn)
                {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "{\"action\":\"media\",\"btn\":\"%s\",\"pin\":\"%s\"}",
                             btn, g_auth_key);
                    network_send_json(msg);
                    audio_play_confirm();
                }
            }
        }
        // ---- SET (mode 4) ----
        else if (g_screen_mode == 4)
        {
            int tc = graphics_theme_count();
            int pc = g_profile_count;

            if (g_set_sub == 0) // ======== MAIN VIEW ========
            {
                // D-UP/DOWN: switch focused row
                if (kDown & KEY_DUP)
                {
                    if (g_set_row != 0)
                    {
                        g_set_row     = 0;
                        g_preview_idx = g_theme_index;
                        set_apply_theme_preview(g_preview_idx);
                        audio_play_click();
                    }
                }
                if (kDown & KEY_DDOWN)
                {
                    if (g_set_row != 1)
                    {
                        // Revert theme to saved before switching to SERVER row
                        graphics_apply_theme(g_theme_index);
                        g_set_row     = 1;
                        g_preview_idx = g_profile_index;
                        audio_play_click();
                    }
                }

                // D-LEFT/RIGHT: rotate options within focused row
                if (kDown & KEY_DLEFT)
                {
                    int cnt = (g_set_row == 0) ? tc : pc;
                    g_preview_idx = (g_preview_idx - 1 + cnt) % cnt;
                    if (g_set_row == 0) set_apply_theme_preview(g_preview_idx);
                    audio_play_click();
                }
                if (kDown & KEY_DRIGHT)
                {
                    int cnt = (g_set_row == 0) ? tc : pc;
                    g_preview_idx = (g_preview_idx + 1) % cnt;
                    if (g_set_row == 0) set_apply_theme_preview(g_preview_idx);
                    audio_play_click();
                }

                // A: apply current preview
                if (kDown & KEY_A)
                {
                    if (g_set_row == 0)
                        set_apply_and_save_theme(g_preview_idx);
                    else
                        set_apply_and_save_server(g_preview_idx);
                }

                // Y: edit current selection
                if (kDown & KEY_Y)
                    set_enter_editor(g_preview_idx);

                // X: open manager
                if (kDown & KEY_X)
                    set_enter_manager();
            }
            else if (g_set_sub == 1) // ======== MANAGER ========
            {
                int count = (g_set_row == 0) ? tc : pc;

                // D-UP/DOWN: navigate list
                if (kDown & KEY_DUP)
                {
                    g_preview_idx = (g_preview_idx - 1 + count) % count;
                    if (g_set_row == 0) set_apply_theme_preview(g_preview_idx);
                    audio_play_click();
                }
                if (kDown & KEY_DDOWN)
                {
                    g_preview_idx = (g_preview_idx + 1) % count;
                    if (g_set_row == 0) set_apply_theme_preview(g_preview_idx);
                    audio_play_click();
                }

                // A: apply selected
                if (kDown & KEY_A)
                {
                    if (g_set_row == 0) set_apply_and_save_theme(g_preview_idx);
                    else                set_apply_and_save_server(g_preview_idx);
                    g_set_sub = 0;
                }

                // Y: edit selected entry
                if (kDown & KEY_Y)
                    set_enter_editor(g_preview_idx);

                // Touch: row tap or DEL
                if (kDown & KEY_TOUCH && touch.px < 260)
                {
                    for (int i = 0; i < count && i < 4; i++)
                    {
                        float ry = 24.0f + i * 46.0f;
                        if (touch.py >= ry && touch.py < ry + 38)
                        {
                            if (touch.px >= 212 && count > 1)
                            {
                                // DEL
                                if (g_set_row == 0)
                                {
                                    graphics_delete_theme(i);
                                    set_apply_theme_preview(g_theme_index);
                                }
                                else
                                {
                                    if (g_profile_count > 1)
                                    {
                                        for (int j = i; j < g_profile_count - 1; j++)
                                            g_profiles[j] = g_profiles[j + 1];
                                        g_profile_count--;
                                        if (g_profile_index >= g_profile_count) g_profile_index = 0;
                                        network_save_profiles();
                                    }
                                }
                                g_preview_idx = clamp_int(g_preview_idx, 0,
                                    (g_set_row == 0 ? graphics_theme_count() : g_profile_count) - 1);
                                audio_play_confirm();
                            }
                            else
                            {
                                g_preview_idx = i;
                                if (g_set_row == 0) set_apply_theme_preview(g_preview_idx);
                                audio_play_click();
                            }
                        }
                    }
                    // [+ NEW] button
                    float ny = 24.0f + 4 * 46.0f;
                    if (ny > 200.0f) ny = 200.0f;
                    if (touch.py >= ny && touch.py < ny + 30)
                    {
                        int new_idx;
                        if (g_set_row == 0)
                            new_idx = graphics_add_theme();
                        else
                        {
                            if (g_profile_count < MAX_SERVER_PROFILES)
                            {
                                g_profiles[g_profile_count] = g_profiles[g_profile_index];
                                snprintf(g_profiles[g_profile_count].name,
                                         sizeof(g_profiles[0].name), "NEW%d", g_profile_count);
                                new_idx = g_profile_count++;
                            }
                            else new_idx = g_profile_count - 1;
                        }
                        set_enter_editor(new_idx);
                    }
                }

                // B: back
                if (kDown & KEY_B)
                {
                    graphics_apply_theme(g_theme_index); // revert preview
                    g_preview_idx = g_theme_index;
                    g_set_sub = 0;
                    audio_play_click();
                }
            }
            else if (g_set_sub == 2) // ======== EDITOR ========
            {
                if (g_set_row == 0) // Theme editor
                {
                    ThemePreset *t = graphics_get_theme_mut(g_editing);
                    int fields = 5; // BG, PANEL, AC1, AC2, DNG

                    // D-UP/DOWN: move between fields
                    if (kDown & KEY_DUP)   { g_edit_field = (g_edit_field - 1 + fields) % fields; audio_play_click(); }
                    if (kDown & KEY_DDOWN) { g_edit_field = (g_edit_field + 1) % fields; audio_play_click(); }

                    if (kDown & KEY_DLEFT)
                    {
                        int pc = graphics_palette_count();
                        g_edit_swatch = (g_edit_swatch - 1 + pc) % pc;
                        audio_play_click();
                    }
                    if (kDown & KEY_DRIGHT)
                    {
                        int pc = graphics_palette_count();
                        g_edit_swatch = (g_edit_swatch + 1) % pc;
                        audio_play_click();
                    }

                    // A: apply swatch to focused field
                    if (kDown & KEY_A && t)
                    {
                        u8 r, g, b;
                        graphics_palette_color(g_edit_swatch, &r, &g, &b);
                        switch (g_edit_field) {
                        case 0: t->bg_r=r;  t->bg_g=g;  t->bg_b=b;  break;
                        case 1: t->pan_r=r; t->pan_g=g; t->pan_b=b;
                                // auto-derive dim = pan * 0.85
                                t->dim_r=(u8)(r*85/100);
                                t->dim_g=(u8)(g*85/100);
                                t->dim_b=(u8)(b*85/100); break;
                        case 2: t->ac1_r=r; t->ac1_g=g; t->ac1_b=b; break;
                        case 3: t->ac2_r=r; t->ac2_g=g; t->ac2_b=b; break;
                        case 4: t->dng_r=r; t->dng_g=g; t->dng_b=b; break;
                        }
                        graphics_apply_theme(g_editing); // live preview of edit
                        audio_play_confirm();
                    }

                    // Touch: name field tap (y=202..220) → swkbd rename
                    if (kDown & KEY_TOUCH && touch.py >= 202 && touch.py < 220 && t)
                    {
                        prompt_for_name("Preset name", t->name, sizeof(t->name));
                        audio_play_click();
                    }

                    // Y: save and go back
                    if (kDown & KEY_Y && t)
                    {
                        g_theme_index = g_editing;
                        graphics_apply_theme(g_theme_index);
                        graphics_save_themes();
                        g_preview_idx = g_theme_index;
                        g_set_sub = 0;
                        audio_play_confirm();
                    }

                    // B: cancel, restore backup
                    if (kDown & KEY_B && t)
                    {
                        *t = g_edit_backup;
                        graphics_apply_theme(g_theme_index);
                        g_set_sub = 0;
                        audio_play_click();
                    }
                }
                else // Server editor
                {
                    if (g_editing < 0 || g_editing >= g_profile_count) { g_set_sub=0; }
                    else
                    {
                        ServerProfile *p = &g_profiles[g_editing];

                        // D-UP/DOWN: navigate fields
                        if (kDown & KEY_DUP)   { g_edit_field = (g_edit_field - 1 + 4) % 4; audio_play_click(); }
                        if (kDown & KEY_DDOWN) { g_edit_field = (g_edit_field + 1) % 4;     audio_play_click(); }

                        // Touch: tap a field row to open swkbd
                        if (kDown & KEY_TOUCH || (kDown & KEY_A))
                        {
                            int field_to_edit = -1;
                            if (kDown & KEY_A)
                            {
                                field_to_edit = g_edit_field; // A edits focused field
                            }
                            else
                            {
                                for (int fi = 0; fi < 4; fi++)
                                {
                                    float ry = 28.0f + fi * 44.0f;
                                    if (touch.py >= ry && touch.py < ry + 36 && touch.px < 260)
                                        { field_to_edit = fi; break; }
                                }
                            }

                            if (field_to_edit >= 0)
                            {
                                switch (field_to_edit) {
                                case 0: prompt_for_name("Profile name", p->name, sizeof(p->name)); break;
                                case 1: prompt_for_ip(p->ip, sizeof(p->ip)); break;
                                case 2: prompt_for_port(&p->port); break;
                                case 3: prompt_for_key(p->pin, sizeof(p->pin)); break;
                                }
                                audio_play_click();
                            }
                        }

                        // Y: save
                        if (kDown & KEY_Y)
                        {
                            set_apply_and_save_server(g_editing);
                            g_set_sub = 0;
                        }

                        // B: cancel (no write)
                        if (kDown & KEY_B) { g_set_sub = 0; audio_play_click(); }
                    }
                }
            }
        }
        // ---- LEVEL (mode 5) ----
        else if (g_screen_mode == 5)
        {
            if (kDown & KEY_X)
                g_control_target = !g_control_target;

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
                                { network_send_level("volume", level); g_last_level_send_time = now; }
                            g_last_level_touch_time = now;
                        }
                    }
                    else if (g_brightness_level != level)
                    {
                        g_brightness_level = level;
                        if (now - g_last_level_send_time > 200)
                            { network_send_level("brightness", level); g_last_level_send_time = now; }
                        g_last_level_touch_time = now;
                    }
                }
            }
            if (kUp & KEY_TOUCH)
            {
                int level = throttle_level_from_touch((float)touch.px, (float)touch.py);
                if (level >= 0)
                {
                    if (g_control_target == 0) network_send_level("volume", level);
                    else                       network_send_level("brightness", level);
                    g_last_level_send_time  = now;
                    g_last_level_touch_time = now;
                }
            }
        }
#ifndef DISABLE_CAM
        // ---- CAM (mode 6) ----
        else if (g_screen_mode == 6)
        {
            if (kDown & KEY_X)
            {
                g_cam_top_screen = !g_cam_top_screen;
                if (g_cam_top_screen)
                {
                    network_cam_send_cmd('T');
                }
                else
                {
                    network_cam_send_cmd('B');
                }
                audio_play_click();
            }
            if ((kDown & KEY_L) || (kDown & KEY_DLEFT))
            {
                network_cam_send_cmd('P');
                if (g_cam_monitor_idx > 0) g_cam_monitor_idx--;
                else g_cam_monitor_idx = 7;
                audio_play_click();
            }
            if ((kDown & KEY_R) || (kDown & KEY_DRIGHT))
            {
                network_cam_send_cmd('N');
                g_cam_monitor_idx = (g_cam_monitor_idx + 1) % 8;
                audio_play_click();
            }
            if (kDown & KEY_A)
            {
                network_cam_send_cmd('Z');
                g_cam_zoom_mode = !g_cam_zoom_mode;
                audio_play_click();
            }
            if (kDown & KEY_Y)
            {
                network_cam_send_cmd('O');
                audio_play_click();
            }
            if ((kDown & KEY_TOUCH) && touch.px < 260 && touch.py < 180)
            {
                g_cam_top_screen = !g_cam_top_screen;
                if (g_cam_top_screen)
                {
                    network_cam_send_cmd('T');
                }
                else
                {
                    network_cam_send_cmd('B');
                }
                audio_play_click();
            }
        }

        // Global top screen swap return
        if (g_cam_top_screen && (kDown & KEY_X) && g_screen_mode != 6)
        {
            g_cam_top_screen = 0;
            network_cam_stop();
            audio_play_click();
        }
#endif
        // ---- CONTROLLER (mode 7) ----
        else if (g_screen_mode == 7)
        {
            // Touch button handling
            if ((kDown & KEY_TOUCH) || (kHeld & KEY_TOUCH))
            {
                // Top Header: [ EXIT CONTROLLER ] button (x: 14..262, y: 10..48)
                if (touch.py <= 50 && touch.px <= 262)
                {
                    g_screen_mode = 0;
                    net_ctrl_stop();
                    audio_play_click();
                }
                // Mapping toggle button (x: 14..124, y: 56..121)
                else if ((kDown & KEY_TOUCH) && touch.px < 130 && touch.py >= 56 && touch.py <= 125)
                {
                    g_ctrl_physical_map = !g_ctrl_physical_map;
                    audio_play_click();
                }
            }

            // Virtual Right Stick Touch Area (x: 134..262, y: 56..215)
            s16 touch_rx_val = 0;
            s16 touch_ry_val = 0;
            if ((kHeld & KEY_TOUCH) && touch.px >= 134 && touch.py >= 56)
            {
                g_ctrl_touch_active = 1;
                g_ctrl_touch_rx = (float)touch.px;
                g_ctrl_touch_ry = (float)touch.py;
                float d_x = (float)touch.px - 220.0f;
                float d_y = (float)touch.py - 130.0f;
                touch_rx_val = (s16)clamp_int((int)(d_x * 780.0f), -32767, 32767);
                touch_ry_val = (s16)clamp_int((int)(-d_y * 780.0f), -32767, 32767);
            }
            else
            {
                g_ctrl_touch_active = 0;
                g_ctrl_touch_rx = 220.0f;
                g_ctrl_touch_ry = 130.0f;
            }

            // Read hardware Circle Pad
            circlePosition circlePos = {0, 0};
            hidCircleRead(&circlePos);
            g_ctrl_cx = (s16)clamp_int((int)circlePos.dx * 215, -32767, 32767);
            g_ctrl_cy = (s16)clamp_int((int)circlePos.dy * 215, -32767, 32767);

            // Read New 3DS C-Stick (only if hardware supported and inited)
            circlePosition cstickPos = {0, 0};
            int cstick_active = 0;
            if (s_irrst_inited)
            {
                irrstCstickRead(&cstickPos);
                cstick_active = (abs((int)cstickPos.dx) > 15 || abs((int)cstickPos.dy) > 15);
            }

            u32 flags = 0;
            if (cstick_active)
            {
                flags |= 0x01; // C-Stick Active
                g_ctrl_rx = (s16)clamp_int((int)cstickPos.dx * 215, -32767, 32767);
                g_ctrl_ry = (s16)clamp_int((int)cstickPos.dy * 215, -32767, 32767);
            }
            else if (g_ctrl_touch_active)
            {
                flags |= 0x02; // Touch Active
                g_ctrl_rx = touch_rx_val;
                g_ctrl_ry = touch_ry_val;
            }
            else
            {
                g_ctrl_rx = 0;
                g_ctrl_ry = 0;
            }

            // Emergency Exit Combo: Hold L + R + SELECT for 1 second
            if ((kHeld & KEY_L) && (kHeld & KEY_R) && (kHeld & KEY_SELECT))
            {
                if (s_emergency_hold_start == 0) s_emergency_hold_start = now;
                u64 held_ms = now - s_emergency_hold_start;
                g_ctrl_emergency_progress = (float)held_ms / 1000.0f;
                if (g_ctrl_emergency_progress >= 1.0f)
                {
                    g_screen_mode = 0;
                    net_ctrl_stop();
                    g_ctrl_emergency_progress = 0.0f;
                    s_emergency_hold_start = 0;
                    audio_play_confirm();
                }
            }
            else
            {
                s_emergency_hold_start = 0;
                g_ctrl_emergency_progress = 0.0f;
            }

            g_ctrl_held_keys = kHeld;
            // 60 Hz UDP tick dispatch
            net_ctrl_send_tick(kHeld, g_ctrl_cx, g_ctrl_cy, g_ctrl_rx, g_ctrl_ry, flags);
        }

        // ----------------------------------------------------------------
        // Pomodoro timer tick
        // ----------------------------------------------------------------
        static time_t lastPomoTime = 0;
        time_t currentTime = time(NULL);
        if (lastPomoTime == 0) lastPomoTime = currentTime;

        if (g_pomodoro_active)
        {
            if (difftime(currentTime, lastPomoTime) >= 1.0)
            {
                g_pomodoro_seconds--;
                if (g_pomodoro_seconds <= 0)
                {
                    g_pomodoro_active  = 0;
                    g_pomodoro_seconds = current_pomodoro_minutes() * 60;
                    char msg[128];
                    snprintf(msg, sizeof(msg), "{\"action\":\"notify\",\"pin\":\"%s\"}", g_auth_key);
                    network_send_json(msg);
                }
                lastPomoTime = currentTime;
            }
        }
        else lastPomoTime = currentTime;

        // ----------------------------------------------------------------
        // Frame throttle (~20 fps for telemetry, ~60 fps for controller mode)
        // ----------------------------------------------------------------
        {
            u64 now_draw = osGetTime();
            u64 interval = (g_screen_mode == 7) ? 16 : FRAME_INTERVAL_MS;
            if (now_draw - g_last_draw_time >= interval)
            {
                g_last_draw_time = now_draw;
                graphics_draw_frame(g_set_sub, g_set_row, g_preview_idx,
                                    g_editing, g_edit_field, g_edit_swatch);
            }
            else svcSleepThread((g_screen_mode == 7 ? 2 : 5) * 1000000LL);
        }
    }

    audio_exit();
    graphics_exit();
    if (s_irrst_inited)
        irrstExit();
    ptmuExit();
    network_exit();
    romfsExit();
    gfxExit();
    return 0;
}