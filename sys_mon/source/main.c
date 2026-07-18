#include <string.h>
#include <stdio.h>
#include <time.h>
#include <3ds.h>

#include "network.h"
#include "graphics.h"
#include "input.h"

int main()
{
    gfxInitDefault();
    romfsInit();
    httpcInit(0);

    char ip_buffer[60] = "192.168.0.6";
    int port = 4201;
    char full_url[256];
    
    int need_prompt = 1;
    FILE* f = fopen("sdmc:/sysmon_cfg.txt", "r");
    if (f) {
        if (fscanf(f, "%59s %d", ip_buffer, &port) == 2) {
            need_prompt = 0;
        }
        fclose(f);
    }

    if (need_prompt) {
        prompt_for_ip(ip_buffer, sizeof(ip_buffer));
        prompt_for_port(&port);
        
        FILE* f_out = fopen("sdmc:/sysmon_cfg.txt", "w");
        if (f_out) {
            fprintf(f_out, "%s %d", ip_buffer, port);
            fclose(f_out);
        }
    }

    snprintf(full_url, sizeof(full_url), "http://%s:%d/api/data", ip_buffer, port);

    graphics_init();

    time_t initialTime = time(NULL);

    while (aptMainLoop())
    {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) break;

        touchPosition touch;
        hidTouchRead(&touch);

        if (kDown & KEY_TOUCH) {
            if (touch.px >= 60 && touch.px <= 260 && touch.py >= 100 && touch.py <= 140) {
                prompt_for_ip(ip_buffer, sizeof(ip_buffer));
                prompt_for_port(&port);
                snprintf(full_url, sizeof(full_url), "http://%s:%d/api/data", ip_buffer, port);
                
                FILE* f_out = fopen("sdmc:/sysmon_cfg.txt", "w");
                if (f_out) {
                    fprintf(f_out, "%s %d", ip_buffer, port);
                    fclose(f_out);
                }
            }
        }

        time_t currentTime = time(NULL);
        if (difftime(currentTime, initialTime) >= 3.0)
        {
            http_fetch(full_url);
            initialTime = currentTime;
        }

        graphics_draw_frame(ip_buffer, port);
    }

    graphics_exit();
    httpcExit();
    romfsExit();
    gfxExit();
    return 0;
}
