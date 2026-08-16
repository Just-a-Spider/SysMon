#include "net_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <3ds/services/soc.h>

ServerProfile g_profiles[MAX_SERVER_PROFILES];
int g_profile_count = 0;
int g_profile_index = 0;
char g_auth_key[16] = "1234";
char g_srv_ip[64] = "192.168.0.1";
int g_srv_port = 7341;

static u32 *soc_buffer = NULL;

Result net_core_init(const char *ip, int port)
{
    if (!soc_buffer)
    {
        soc_buffer = (u32 *)memalign(0x1000, 0x20000);
        if (!soc_buffer)
            return -1;
        Result ret = socInit(soc_buffer, 0x20000);
        if (R_FAILED(ret))
        {
            free(soc_buffer);
            soc_buffer = NULL;
            return ret;
        }
    }

    if (ip && ip[0])
        strncpy(g_srv_ip, ip, sizeof(g_srv_ip) - 1);
    g_srv_port = port;

    return 0;
}

void net_core_exit(void)
{
    if (soc_buffer)
    {
        socExit();
        free(soc_buffer);
        soc_buffer = NULL;
    }
}

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
        // Migrate immediately
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
        if (line[0] == '#') continue;
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
