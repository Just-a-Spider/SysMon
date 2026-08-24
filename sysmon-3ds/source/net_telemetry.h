#ifndef NET_TELEMETRY_H
#define NET_TELEMETRY_H

#include <3ds.h>

typedef struct
{
    u32 pid;
    char name[32];
    float cpu;
} ProcessInfo;

typedef struct
{
    char button[16];
    char type[8];
    char label[32];
    char color[16];
} MacroInfo;

extern float g_cpu_temp;
extern float g_gpu_temp;
extern char g_gpu_name[32];
extern float g_free_ram;
extern float g_cpu_usage;
extern int g_cpu_fan;
extern int g_gpu_fan;
extern int g_http_status;
extern int g_fetching_enabled;
extern Result g_last_error;
extern float g_temp_history[10];
extern float g_gpu_temp_history[10];
extern int g_history_count;

extern ProcessInfo g_top_procs[5];
extern int g_proc_count;
extern int g_has_notification;
extern char g_weather[32];

extern MacroInfo g_macros[12];
extern int g_macro_count;
extern char g_now_playing[64];
extern u32 g_kill_confirm_pid;
extern char g_kill_confirm_name[32];
extern u64 g_last_level_touch_time;

Result net_telemetry_start(void);
void   net_telemetry_stop(void);
void   network_send_json(const char *json_str);
void   network_send_level(const char *target, int value);

#endif
