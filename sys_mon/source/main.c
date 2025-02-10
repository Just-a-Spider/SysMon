#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <time.h>

#include <3ds.h>

Result http_fetch(const char *url) {
    Result ret = 0;
    httpcContext context;
    u32 statuscode = 0;
    u32 contentsize = 0, readsize = 0;
    u8 *buf;

    // Initialize the HTTP context
    ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 1);
    if (ret != 0) {
        printf("httpcOpenContext failed: %" PRId32 "\n", ret);
        return ret;
    }

    // Enable Keep-Alive connections
    ret = httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
    if (ret != 0) {
        printf("httpcSetKeepAlive failed: %" PRId32 "\n", ret);
        httpcCloseContext(&context);
        return ret;
    }

    // Begin the request
    ret = httpcBeginRequest(&context);
    if (ret != 0) {
        printf("httpcBeginRequest failed: %" PRId32 "\n", ret);
        httpcCloseContext(&context);
        return ret;
    }

    // Get the response status code
    ret = httpcGetResponseStatusCode(&context, &statuscode);
    if (ret != 0) {
        printf("httpcGetResponseStatusCode failed: %" PRId32 "\n", ret);
        httpcCloseContext(&context);
        return ret;
    }

    if (statuscode != 200) {
        printf("HTTP request failed with status code: %" PRId32 "\n", statuscode);
        httpcCloseContext(&context);
        return -1;
    }

    // Get the content size
    ret = httpcGetDownloadSizeState(&context, NULL, &contentsize);
    if (ret != 0) {
        printf("httpcGetDownloadSizeState failed: %" PRId32 "\n", ret);
        httpcCloseContext(&context);
        return ret;
    }

    // Allocate buffer for the response
    buf = (u8*)malloc(contentsize);
    if (buf == NULL) {
        printf("Failed to allocate memory\n");
        httpcCloseContext(&context);
        return -1;
    }

    // Read the response data
    ret = httpcDownloadData(&context, buf, contentsize, &readsize);
    if (ret != 0) {
        printf("httpcDownloadData failed: %" PRId32 "\n", ret);
        free(buf);
        httpcCloseContext(&context);
        return ret;
    }

    buf[contentsize] = '\0'; // Null-terminate the buffer
    
    // Segmentate the buf response (JSON) for readable data
    int cpu_fan = 0;
    int gpu_fan = 0;
    float gpu_temp = 0.0;
    float cpu_temp = 0.0;
    float free_ram = 0.0;
    float cpu_usage = 0.0;
    
    char *cpu_fan_str = strstr((char *)buf, "\"cpu_fan\":");
    if (cpu_fan_str) {
        sscanf(cpu_fan_str, "\"cpu_fan\":%d", &cpu_fan);
    }
    
    char *gpu_fan_str = strstr((char *)buf, "\"gpu_fan\":");
    if (gpu_fan_str) {
        sscanf(gpu_fan_str, "\"gpu_fan\":%d", &gpu_fan);
    }
    
    // Use the correct key "gpu_temp" now
    char *gpu_temp_str = strstr((char *)buf, "\"gpu_temp\":");
    if (gpu_temp_str) {
        sscanf(gpu_temp_str, "\"gpu_temp\":%f", &gpu_temp);
    }
    
    char *cpu_temp_str = strstr((char *)buf, "\"cpu_temp\":");
    if (cpu_temp_str) {
        sscanf(cpu_temp_str, "\"cpu_temp\":%f", &cpu_temp);
    }
    
    char *free_ram_str = strstr((char *)buf, "\"free_ram\":");
    if (free_ram_str) {
        sscanf(free_ram_str, "\"free_ram\":%f", &free_ram);
    }
    
    // Parse cpu_usage as a quoted string
    char *cpu_usage_str = strstr((char *)buf, "\"cpu_usage\":");
    if (cpu_usage_str) {
        char cpu_usage_val[10];
        sscanf(cpu_usage_str, "\"cpu_usage\":\"%[^\"]\"", cpu_usage_val);
        cpu_usage = strtof(cpu_usage_val, NULL);
    }
    
    // Print the response data
    printf("\x1b[1;0HSystem Data:");
    printf("\x1b[2;0HRAM Available: %.2f GB", free_ram);
    printf("\x1b[3;0HCPU Usage: %.2f%%", cpu_usage);
    printf("\x1b[4;0HCPU Temps: %.1f C", cpu_temp);
    printf("\x1b[5;0HGPU Temps: %.1f C", gpu_temp);
    printf("\x1b[6;0HCPU Fan: %d RPM", cpu_fan);
    printf("\x1b[7;0HGPU Fan: %d RPM", gpu_fan);
    
    // Clean up
    free(buf);
    httpcCloseContext(&context);
    
    return 0;
}

int main()
{
    Result ret = 0;
    gfxInitDefault();
    httpcInit(0); // Buffer size when POST/PUT.

    consoleInit(GFX_TOP, NULL);

	printf("\x1b[30;16HPress Start to exit.");

    // Store the initial time
    time_t initialTime = time(NULL);

    // Main loop
    while (aptMainLoop())
    {
        gspWaitForVBlank();
        hidScanInput();

        // Get the current time
        time_t currentTime = time(NULL);
        double elapsedSeconds = difftime(currentTime, initialTime);

        // Check if 3 seconds have passed
        if (elapsedSeconds >= 3.0)
        {
            // Run the fetch function
            ret = http_fetch("http://192.168.0.6:4201/api/data");
            printf("\x1b[8;0Hreturn from http_fetch: %" PRId32 "\n", ret);

            // Reset the initial time
            initialTime = currentTime;
        }

        // Check for the start button press to quit
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START)
            break; // break in order to return to hbmenu
    }

    // Exit services
    httpcExit();
    gfxExit();
    return 0;
}
