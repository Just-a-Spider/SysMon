#include "network.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

float g_cpu_temp = 0.0f;
float g_gpu_temp = 0.0f;
float g_free_ram = 0.0f;
float g_cpu_usage = 0.0f;
int g_cpu_fan = 0;
int g_gpu_fan = 0;
int g_http_status = 0;
float g_temp_history[10] = {0};
int g_history_count = 0;

static int call_counter = 0;

Result http_fetch(const char *url) {
    Result ret = 0;
    httpcContext context;
    u32 statuscode = 0;
    u32 contentsize = 0, readsize = 0;
    u8 *buf;

    ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 1);
    if (ret != 0) return ret;

    ret = httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
    if (ret != 0) { httpcCloseContext(&context); return ret; }

    ret = httpcBeginRequest(&context);
    if (ret != 0) { httpcCloseContext(&context); return ret; }

    ret = httpcGetResponseStatusCode(&context, &statuscode);
    g_http_status = statuscode;
    
    if (ret != 0 || statuscode != 200) {
        httpcCloseContext(&context);
        return -1;
    }

    ret = httpcGetDownloadSizeState(&context, NULL, &contentsize);
    if (ret != 0) { httpcCloseContext(&context); return ret; }

    buf = (u8*)malloc(contentsize + 1);
    if (buf == NULL) { httpcCloseContext(&context); return -1; }

    ret = httpcDownloadData(&context, buf, contentsize, &readsize);
    if (ret != 0) { free(buf); httpcCloseContext(&context); return ret; }

    buf[contentsize] = '\0';
    
    char *cpu_fan_str = strstr((char *)buf, "\"cpu_fan\":");
    if (cpu_fan_str) sscanf(cpu_fan_str, "\"cpu_fan\":%d", &g_cpu_fan);
    
    char *gpu_fan_str = strstr((char *)buf, "\"gpu_fan\":");
    if (gpu_fan_str) sscanf(gpu_fan_str, "\"gpu_fan\":%d", &g_gpu_fan);
    
    char *gpu_temp_str = strstr((char *)buf, "\"gpu_temp\":");
    if (gpu_temp_str) sscanf(gpu_temp_str, "\"gpu_temp\":%f", &g_gpu_temp);
    
    char *cpu_temp_str = strstr((char *)buf, "\"cpu_temp\":");
    if (cpu_temp_str) sscanf(cpu_temp_str, "\"cpu_temp\":%f", &g_cpu_temp);
    
    char *free_ram_str = strstr((char *)buf, "\"free_ram\":");
    if (free_ram_str) sscanf(free_ram_str, "\"free_ram\":%f", &g_free_ram);
    
    char *cpu_usage_str = strstr((char *)buf, "\"cpu_usage\":");
    if (cpu_usage_str) {
        char cpu_usage_val[10];
        sscanf(cpu_usage_str, "\"cpu_usage\":\"%[^\"]\"", cpu_usage_val);
        g_cpu_usage = strtof(cpu_usage_val, NULL);
    }
    
    free(buf);
    httpcCloseContext(&context);

    // Save history every 20 calls
    call_counter++;
    if (call_counter >= 20 || g_history_count == 0) {
        call_counter = 0;
        if (g_history_count < 10) {
            g_temp_history[g_history_count] = g_cpu_temp;
            g_history_count++;
        } else {
            for (int i = 0; i < 9; i++) {
                g_temp_history[i] = g_temp_history[i+1];
            }
            g_temp_history[9] = g_cpu_temp;
        }
    }

    return 0;
}
