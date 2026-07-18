#pragma once
#include <3ds.h>

extern float g_cpu_temp;
extern float g_gpu_temp;
extern float g_free_ram;
extern float g_cpu_usage;
extern int g_cpu_fan;
extern int g_gpu_fan;
extern int g_http_status;
extern float g_temp_history[10];
extern int g_history_count;

Result http_fetch(const char *url);
