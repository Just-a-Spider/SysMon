#include "net_telemetry.h"
#include "net_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <3ds/thread.h>
#include <3ds/svc.h>
#include <3ds/synchronization.h>
#include <netinet/tcp.h>
#include <errno.h>

float g_cpu_temp = 0.0f;
float g_gpu_temp = 0.0f;
char g_gpu_name[32] = "GPU";
float g_free_ram = 0.0f;
float g_cpu_usage = 0.0f;
int g_cpu_fan = 0;
int g_gpu_fan = 0;
int g_http_status = 0;
int g_fetching_enabled = 1;
Result g_last_error = 0;
float g_temp_history[10] = {0};
float g_gpu_temp_history[10] = {0};
int g_history_count = 0;

ProcessInfo g_top_procs[5];
int g_proc_count = 0;
int g_has_notification = 0;
char g_weather[32] = "--";

MacroInfo g_macros[12];
int g_macro_count = 0;
char g_now_playing[64] = "";
u32 g_kill_confirm_pid = 0;
char g_kill_confirm_name[32] = "";
u64 g_last_level_touch_time = 0;

static int g_socket = -1;
static Thread net_thread = NULL;
static volatile int g_net_running = 0;
static LightLock send_lock;

static void parse_telemetry(const char *json)
{
    extern int g_volume_level;
    extern int g_brightness_level;
#ifndef DISABLE_CAM
    extern int g_stream_port;
#endif
    u64 now = osGetTime();
    if (now - g_last_level_touch_time > 2000)
    {
        char *vol_ptr = strstr(json, "\"volume\":");
        if (vol_ptr)
        {
            int v = 0;
            if (sscanf(vol_ptr, "\"volume\":%d", &v) == 1)
                g_volume_level = v;
        }

        char *br_ptr = strstr(json, "\"brightness\":");
        if (br_ptr)
        {
            int b = 0;
            if (sscanf(br_ptr, "\"brightness\":%d", &b) == 1)
                g_brightness_level = b;
        }
    }

#ifndef DISABLE_CAM
    char *stream_port_str = strstr(json, "\"stream_port\":");
    if (stream_port_str)
        sscanf(stream_port_str, "\"stream_port\":%d", &g_stream_port);
#endif

    char *cpu_fan_str = strstr(json, "\"cpu_fan\":");
    if (cpu_fan_str)
        sscanf(cpu_fan_str, "\"cpu_fan\":%d", &g_cpu_fan);

    char *gpu_fan_str = strstr(json, "\"gpu_fan\":");
    if (gpu_fan_str)
        sscanf(gpu_fan_str, "\"gpu_fan\":%d", &g_gpu_fan);

    char *cpu_temp_str = strstr(json, "\"cpu_temp\":");
    if (cpu_temp_str)
        sscanf(cpu_temp_str, "\"cpu_temp\":%f", &g_cpu_temp);

    char *gpu_temp_str = strstr(json, "\"gpu_temp\":");
    if (gpu_temp_str)
        sscanf(gpu_temp_str, "\"gpu_temp\":%f", &g_gpu_temp);

    char *gpu_name_str = strstr(json, "\"gpu_name\":");
    if (gpu_name_str)
    {
        char val[32] = {0};
        if (sscanf(gpu_name_str, "\"gpu_name\":\"%31[^\"]\"", val) == 1)
        {
            snprintf(g_gpu_name, sizeof(g_gpu_name), "%s", val);
        }
    }

    char *free_ram_str = strstr(json, "\"free_ram\":");
    if (free_ram_str)
        sscanf(free_ram_str, "\"free_ram\":%f", &g_free_ram);

    char *cpu_usage_str = strstr(json, "\"cpu_usage\":");
    if (cpu_usage_str)
    {
        char cpu_usage_val[10] = {0};
        sscanf(cpu_usage_str, "\"cpu_usage\":\"%9[^\"]\"", cpu_usage_val);
        g_cpu_usage = strtof(cpu_usage_val, NULL);
    }

    char *procs_str = strstr((char *)json, "\"top_procs\":[");
    if (procs_str)
    {
        g_proc_count = 0;
        char *ptr = strstr(procs_str, "{");
        while (ptr && g_proc_count < 5)
        {
            char *end = strstr(ptr, "}");
            if (!end)
                break;

            char block[256] = {0};
            int copy_len = end - ptr;
            if (copy_len > 255)
                copy_len = 255;
            strncpy(block, ptr, copy_len);

            int pid = 0;
            char name[32] = {0};
            float cpu = 0.0f;

            char *pid_ptr = strstr(block, "\"pid\":");
            if (pid_ptr)
                sscanf(pid_ptr, "\"pid\":%d", &pid);

            char *name_ptr = strstr(block, "\"name\":\"");
            if (name_ptr)
                sscanf(name_ptr, "\"name\":\"%31[^\"]\"", name);

            char *cpu_ptr = strstr(block, "\"cpu_percent\":");
            if (cpu_ptr)
                sscanf(cpu_ptr, "\"cpu_percent\":%f", &cpu);

            g_top_procs[g_proc_count].pid = pid;
            snprintf(g_top_procs[g_proc_count].name, sizeof(g_top_procs[g_proc_count].name), "%s", name);
            g_top_procs[g_proc_count].cpu = cpu;

            g_proc_count++;
            ptr = strstr(end, "{");
        }
    }

    char *notif_str = strstr((char *)json, "\"has_notification\":");
    if (notif_str)
    {
        char notif_val[10] = {0};
        sscanf(notif_str, "\"has_notification\":%9[^,}]", notif_val);
        g_has_notification = (strstr(notif_val, "true") != NULL);
    }

    char *weather_str = strstr((char *)json, "\"weather\"");
    if (weather_str)
    {
        char *start = strchr(weather_str + 9, '\"');
        if (start)
        {
            sscanf(start, "\"%31[^\"]\"", g_weather);
        }
    }

    char *playing_str = strstr((char *)json, "\"now_playing\"");
    if (playing_str)
    {
        char *start = strchr(playing_str + 13, '\"');
        if (start)
        {
            sscanf(start, "\"%63[^\"]\"", g_now_playing);
        }
    }

    static int call_counter = 0;
    call_counter++;
    if (call_counter >= 20 || g_history_count == 0)
    {
        if (g_history_count < 10)
        {
            g_temp_history[g_history_count] = g_cpu_temp;
            g_gpu_temp_history[g_history_count] = g_gpu_temp;
            g_history_count++;
        }
        else
        {
            for (int i = 0; i < 9; i++)
            {
                g_temp_history[i] = g_temp_history[i + 1];
                g_gpu_temp_history[i] = g_gpu_temp_history[i + 1];
            }
            g_temp_history[9] = g_cpu_temp;
            g_gpu_temp_history[9] = g_gpu_temp;
        }
        call_counter = 0;
    }
}

static void parse_macros(const char *json)
{
    char *data = strstr((char *)json, "\"data\":[");
    if (!data)
        return;

    g_macro_count = 0;
    char *ptr = strstr(data, "{");
    while (ptr && g_macro_count < 12)
    {
        char *end = strstr(ptr, "}");
        if (!end)
            break;

        char block[256] = {0};
        int copy_len = end - ptr;
        if (copy_len > 255)
            copy_len = 255;
        strncpy(block, ptr, copy_len);

        char button[16] = {0};
        char label[32] = {0};
        char color[16] = {0};

        char *btn_ptr = strstr(block, "\"button\":\"");
        if (btn_ptr)
            sscanf(btn_ptr, "\"button\":\"%15[^\"]\"", button);

        char *lbl_ptr = strstr(block, "\"label\":\"");
        if (lbl_ptr)
            sscanf(lbl_ptr, "\"label\":\"%31[^\"]\"", label);

        char *clr_ptr = strstr(block, "\"color\":\"");
        if (clr_ptr)
            sscanf(clr_ptr, "\"color\":\"%15[^\"]\"", color);

        snprintf(g_macros[g_macro_count].button, sizeof(g_macros[g_macro_count].button), "%s", button);
        snprintf(g_macros[g_macro_count].label, sizeof(g_macros[g_macro_count].label), "%s", label);
        snprintf(g_macros[g_macro_count].color, sizeof(g_macros[g_macro_count].color), "%s", color);

        g_macro_count++;
        ptr = strstr(end, "{");
    }
}

static void net_thread_func(void *arg)
{
    extern int g_screen_mode;
    while (g_net_running)
    {
        if (!g_fetching_enabled || g_screen_mode == 7)
        {
            if (g_socket >= 0)
            {
                close(g_socket);
                g_socket = -1;
            }
            svcSleepThread(100 * 1000000LL);
            continue;
        }

        g_http_status = 0; // connecting
        g_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (g_socket < 0)
        {
            g_http_status = -1;
            svcSleepThread(1000 * 1000000LL);
            continue;
        }

        struct sockaddr_in srv_addr;
        memset(&srv_addr, 0, sizeof(srv_addr));
        srv_addr.sin_family = AF_INET;
        srv_addr.sin_port = htons(g_srv_port);
        inet_pton(AF_INET, g_srv_ip, &srv_addr.sin_addr);

        if (connect(g_socket, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0)
        {
            close(g_socket);
            g_socket = -1;
            g_http_status = -1;
            svcSleepThread(1000 * 1000000LL);
            continue;
        }

        int flag = 1;
        setsockopt(g_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));

        int flags = fcntl(g_socket, F_GETFL, 0);
        fcntl(g_socket, F_SETFL, flags | O_NONBLOCK);

        g_http_status = 1; // connected
        char buf[8192];
        memset(buf, 0, sizeof(buf));
        int total_len = 0;

        while (g_net_running && g_fetching_enabled && g_screen_mode != 7)
        {
            int recvd = recv(g_socket, buf + total_len, sizeof(buf) - total_len - 1, 0);

            if (recvd < 0)
            {
                if (errno == EWOULDBLOCK || errno == EAGAIN)
                {
                    svcSleepThread(50 * 1000000LL); // 50ms sleep
                    continue;
                }
                break; // Socket error
            }
            else if (recvd == 0)
            {
                break; // Connection closed
            }

            total_len += recvd;
            buf[total_len] = '\0';

            char *nl = strchr(buf, '\n');
            while (nl)
            {
                *nl = '\0';

                if (strstr(buf, "\"type\":\"macros\""))
                {
                    parse_macros(buf);
                }
                else
                {
                    parse_telemetry(buf);
                }

                int line_len = (nl - buf) + 1;
                int remaining = total_len - line_len;
                if (remaining > 0)
                {
                    memmove(buf, nl + 1, remaining);
                    total_len = remaining;
                    buf[total_len] = '\0';
                }
                else
                {
                    total_len = 0;
                    buf[0] = '\0';
                }

                nl = strchr(buf, '\n');
            }

            svcSleepThread(10 * 1000000LL);
        }

        if (g_socket >= 0)
        {
            close(g_socket);
            g_socket = -1;
        }
        g_http_status = -1;
        svcSleepThread(1000 * 1000000LL);
    }
}

Result net_telemetry_start(void)
{
    LightLock_Init(&send_lock);
    g_net_running = 1;
    net_thread = threadCreate(net_thread_func, NULL, 32768, 0x3f, -2, false);
    return net_thread ? 0 : -1;
}

void net_telemetry_stop(void)
{
    g_net_running = 0;
    if (g_socket >= 0)
        close(g_socket);
    if (net_thread)
    {
        threadJoin(net_thread, U64_MAX);
        threadFree(net_thread);
        net_thread = NULL;
    }
}

void network_send_json(const char *json_str)
{
    if (g_socket < 0)
        return;

    LightLock_Lock(&send_lock);
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s\n", json_str);
    send(g_socket, buf, strlen(buf), 0);
    LightLock_Unlock(&send_lock);
}

void network_send_level(const char *target, int value)
{
    char msg[128];
    snprintf(msg, sizeof(msg), "{\"action\":\"set_level\",\"target\":\"%s\",\"value\":%d,\"pin\":\"%s\"}", target, value, g_auth_key);
    network_send_json(msg);
}
