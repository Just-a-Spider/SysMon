#include "network.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <malloc.h>
#include <3ds/thread.h>
#include <3ds/svc.h>
#include <3ds/synchronization.h>
#include <3ds/synchronization.h>
#include <3ds/services/soc.h>
#include <netinet/tcp.h>
#include <errno.h>

float g_cpu_temp = 0.0f;
float g_gpu_temp = 0.0f;
float g_free_ram = 0.0f;
float g_cpu_usage = 0.0f;
int g_cpu_fan = 0;
int g_gpu_fan = 0;
int g_http_status = 0; // 0=connecting, 1=connected, -1=error
int g_fetching_enabled = 1;
Result g_last_error = 0;
float g_temp_history[10] = {0};
float g_gpu_temp_history[10] = {0};
int g_history_count = 0;

ProcessInfo g_top_procs[5];
int g_proc_count = 0;
int g_has_notification = 0;
char g_weather[32] = "--";
char g_auth_key[16] = "1234";

MacroInfo g_macros[12];
int g_macro_count = 0;
char g_now_playing[64] = "";
u32 g_kill_confirm_pid = 0;
char g_kill_confirm_name[32] = "";

static u32 *soc_buffer = NULL;
static int g_socket = -1;
static Thread net_thread = NULL;
static volatile int g_net_running = 0;
static LightLock send_lock;

static char srv_ip[64];
static int srv_port = 7341;

// ---------------------------------------------------------------------------
// Server profiles
// ---------------------------------------------------------------------------
ServerProfile g_profiles[MAX_SERVER_PROFILES];
int g_profile_count = 0;
int g_profile_index = 0;

void network_load_profiles(void)
{
    FILE *f = fopen("sdmc:/sysmon_cfg.txt", "r");
    if (!f)
    {
        // First run: write single default entry
        g_profile_count = 1;
        snprintf(g_profiles[0].name, sizeof(g_profiles[0].name), "HOME");
        snprintf(g_profiles[0].ip,   sizeof(g_profiles[0].ip),   "192.168.0.1");
        g_profiles[0].port = 7341;
        snprintf(g_profiles[0].pin,  sizeof(g_profiles[0].pin),  "1234");
        g_profile_index = 0;
        snprintf(g_auth_key, sizeof(g_auth_key), "%s", g_profiles[0].pin);
        return;
    }

    // Peek first token to distinguish old format (bare IP) from new ("# active")
    char line[128];
    if (!fgets(line, sizeof(line), f))
    {
        fclose(f);
        return;
    }

    // Old format: first line is an IP address (no '#')
    if (line[0] != '#')
    {
        // Parse: ip port pin
        char ip[60] = "192.168.0.1";
        int  port   = 7341;
        char pin[16] = "1234";
        sscanf(line, "%59s %d %15s", ip, &port, pin);
        g_profile_count = 1;
        snprintf(g_profiles[0].name, sizeof(g_profiles[0].name), "HOME");
        snprintf(g_profiles[0].ip,  sizeof(g_profiles[0].ip),  "%s", ip);
        g_profiles[0].port = port;
        snprintf(g_profiles[0].pin, sizeof(g_profiles[0].pin), "%s", pin);
        g_profile_index = 0;
        snprintf(g_auth_key, sizeof(g_auth_key), "%s", pin);
        fclose(f);
        // Migrate: immediately overwrite with new format
        network_save_profiles();
        return;
    }

    // New format: first line is "# active_index  num_profiles"
    int active = 0, count = 0;
    sscanf(line + 1, "%d %d", &active, &count);
    if (count < 1 || count > MAX_SERVER_PROFILES)
        count = 1;

    g_profile_count = 0;
    while (g_profile_count < count && fgets(line, sizeof(line), f))
    {
        if (line[0] == '#') continue; // skip comment lines
        ServerProfile *p = &g_profiles[g_profile_count];
        char name[16], ip[60], pin[16];
        int  port = 7341;
        if (sscanf(line, "%15s %59s %d %15s", name, ip, &port, pin) >= 3)
        {
            snprintf(p->name, sizeof(p->name), "%s", name);
            snprintf(p->ip,   sizeof(p->ip),   "%s", ip);
            p->port = port;
            snprintf(p->pin,  sizeof(p->pin),  "%s", pin);
            g_profile_count++;
        }
    }
    fclose(f);

    if (g_profile_count == 0)
    {
        // Fallback
        g_profile_count = 1;
        snprintf(g_profiles[0].name, sizeof(g_profiles[0].name), "HOME");
        snprintf(g_profiles[0].ip,   sizeof(g_profiles[0].ip),   "192.168.0.1");
        g_profiles[0].port = 7341;
        snprintf(g_profiles[0].pin,  sizeof(g_profiles[0].pin),  "1234");
        active = 0;
    }

    g_profile_index = (active >= 0 && active < g_profile_count) ? active : 0;
    strncpy(g_auth_key, g_profiles[g_profile_index].pin, sizeof(g_auth_key) - 1);
}

void network_save_profiles(void)
{
    FILE *f = fopen("sdmc:/sysmon_cfg.txt", "w");
    if (!f) return;
    fprintf(f, "# %d %d\n", g_profile_index, g_profile_count);
    for (int i = 0; i < g_profile_count; i++)
    {
        fprintf(f, "%s %s %d %s\n",
                g_profiles[i].name,
                g_profiles[i].ip,
                g_profiles[i].port,
                g_profiles[i].pin);
    }
    fclose(f);
}

static void parse_telemetry(const char *json)
{
    extern int g_volume_level;
    extern int g_brightness_level;
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
    while (g_net_running)
    {
        if (!g_fetching_enabled)
        {
            svcSleepThread(500 * 1000000LL);
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
        srv_addr.sin_port = htons(srv_port);
        inet_pton(AF_INET, srv_ip, &srv_addr.sin_addr);

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

        while (g_net_running && g_fetching_enabled)
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

            // Check for newline which separates JSON packets
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

                int remaining = total_len - ((nl + 1) - buf);
                memmove(buf, nl + 1, remaining);
                total_len = remaining;
                buf[total_len] = '\0';

                nl = strchr(buf, '\n');
            }
        }

        close(g_socket);
        g_socket = -1;
        g_http_status = -1;
    }
}

Result network_init(const char *ip, int port)
{
    soc_buffer = (u32 *)memalign(0x1000, 0x20000);
    if (!soc_buffer)
        return -1;
    socInit(soc_buffer, 0x20000);

    strncpy(srv_ip, ip, sizeof(srv_ip) - 1);
    srv_port = port;

    LightLock_Init(&send_lock);

    g_net_running = 1;
    net_thread = threadCreate(net_thread_func, NULL, 32768, 0x3f, -2, false);
    return 0;
}

void network_exit()
{
    g_net_running = 0;
    if (g_socket >= 0)
        close(g_socket);
    if (net_thread)
    {
        threadJoin(net_thread, U64_MAX);
        threadFree(net_thread);
    }
    socExit();
    if (soc_buffer)
        free(soc_buffer);
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
