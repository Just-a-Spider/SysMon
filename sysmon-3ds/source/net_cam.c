#ifndef DISABLE_CAM
#include "net_cam.h"
#include "net_core.h"
#include "graphics.h"
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
#include <3ds/os.h>
#include <netinet/tcp.h>
#include <errno.h>

int g_stream_port = 7340;
int g_stream_active = 0;
int g_cam_top_screen = 0;
int g_cam_zoom_mode = 0;
int g_cam_monitor_idx = 0;
int g_cam_fps = 0;
char g_cam_source_name[32] = "DISPLAY 1";
u64 g_cam_last_frame_time = 0;

static int g_cam_socket = -1;
static Thread cam_thread = NULL;
static volatile int g_cam_running = 0;
static volatile int g_cam_want_stream = 0;
static volatile int g_cam_reconnect_req = 0;

static u8 frame_payload[512 * 256 * 2 + 65536];

static void cam_thread_func(void *arg)
{
    int frame_fps_counter = 0;
    u64 last_fps_time = osGetTime();

    while (g_cam_running)
    {
        if (g_cam_reconnect_req)
        {
            g_cam_reconnect_req = 0;
            if (g_cam_socket >= 0)
            {
                shutdown(g_cam_socket, SHUT_RDWR);
                close(g_cam_socket);
                g_cam_socket = -1;
            }
        }

        if (!g_cam_want_stream)
        {
            if (g_cam_socket >= 0)
            {
                shutdown(g_cam_socket, SHUT_RDWR);
                close(g_cam_socket);
                g_cam_socket = -1;
            }
            svcSleepThread(50 * 1000000LL);
            continue;
        }

        if (g_cam_socket < 0)
        {
            g_cam_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (g_cam_socket < 0)
            {
                for (int i = 0; i < 5 && g_cam_running && g_cam_want_stream && !g_cam_reconnect_req; i++)
                    svcSleepThread(100 * 1000000LL);
                continue;
            }

            int nodelay = 1;
            setsockopt(g_cam_socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(int));

            int rcvbuf = 65536;
            setsockopt(g_cam_socket, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(int));

            // Set non-blocking before connect for timeout handling & responsive cancellation
            int flags = fcntl(g_cam_socket, F_GETFL, 0);
            fcntl(g_cam_socket, F_SETFL, flags | O_NONBLOCK);

            struct sockaddr_in srv_addr;
            memset(&srv_addr, 0, sizeof(srv_addr));
            srv_addr.sin_family = AF_INET;
            srv_addr.sin_port = htons(g_stream_port);
            inet_pton(AF_INET, g_srv_ip, &srv_addr.sin_addr);

            int c_res = connect(g_cam_socket, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
            int cam_connected = 0;

            if (c_res == 0)
            {
                cam_connected = 1;
            }
            else if (errno == EINPROGRESS)
            {
                for (int elapsed = 0; elapsed < 4000; elapsed += 100)
                {
                    if (!g_cam_running || !g_cam_want_stream || g_cam_reconnect_req)
                        break;

                    fd_set wset;
                    FD_ZERO(&wset);
                    FD_SET(g_cam_socket, &wset);

                    struct timeval tv;
                    tv.tv_sec = 0;
                    tv.tv_usec = 100000;

                    int sel_res = select(g_cam_socket + 1, NULL, &wset, NULL, &tv);
                    if (sel_res > 0)
                    {
                        struct sockaddr_in peer;
                        socklen_t peer_len = sizeof(peer);
                        if (getpeername(g_cam_socket, (struct sockaddr *)&peer, &peer_len) == 0)
                        {
                            cam_connected = 1;
                        }
                        break;
                    }
                    else if (sel_res < 0)
                    {
                        break;
                    }
                }
            }

            if (!cam_connected || !g_cam_running || !g_cam_want_stream || g_cam_reconnect_req)
            {
                shutdown(g_cam_socket, SHUT_RDWR);
                close(g_cam_socket);
                g_cam_socket = -1;
                for (int i = 0; i < 15 && g_cam_running && g_cam_want_stream && !g_cam_reconnect_req; i++)
                    svcSleepThread(100 * 1000000LL);
                continue;
            }

            // Start stream in Delta Tile mode ('SD')
            send(g_cam_socket, "SD", 2, 0);
        }

        u8 hdr[46];
        int hdr_read = 0;
        int err = 0;

        while (hdr_read < 46 && g_cam_running && g_cam_want_stream && !g_cam_reconnect_req)
        {
            int r = recv(g_cam_socket, hdr + hdr_read, 46 - hdr_read, 0);
            if (r < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    svcSleepThread(5 * 1000000LL); // 5ms sleep
                    continue;
                }
                err = 1;
                break;
            }
            else if (r == 0)
            {
                err = 1;
                break;
            }
            hdr_read += r;
        }

        if (err || !g_cam_want_stream || !g_cam_running || g_cam_reconnect_req)
        {
            if (g_cam_socket >= 0)
            {
                shutdown(g_cam_socket, SHUT_RDWR);
                close(g_cam_socket);
                g_cam_socket = -1;
            }
            continue;
        }

        if (hdr_read < 46)
        {
            continue;
        }

        if (memcmp(hdr, "SCAM", 4) != 0)
        {
            // Protocol desync
            shutdown(g_cam_socket, SHUT_RDWR);
            close(g_cam_socket);
            g_cam_socket = -1;
            continue;
        }

        u16 w = (hdr[4] << 8) | hdr[5];
        u16 h = (hdr[6] << 8) | hdr[7];
        u8 fmt = hdr[8];
        g_cam_zoom_mode = hdr[10];

        memcpy(g_cam_source_name, hdr + 12, 30);
        g_cam_source_name[30] = '\0';

        u32 payload_len = ((u32)hdr[42] << 24) | ((u32)hdr[43] << 16) | ((u32)hdr[44] << 8) | (u32)hdr[45];

        if (payload_len > sizeof(frame_payload))
        {
            shutdown(g_cam_socket, SHUT_RDWR);
            close(g_cam_socket);
            g_cam_socket = -1;
            continue;
        }

        if (payload_len == 0)
        {
            // Heartbeat / Keepalive (No new dirty tiles)
            g_cam_last_frame_time = osGetTime();
        }
        else
        {
            u32 total_payload = 0;
            while (total_payload < payload_len && g_cam_running && g_cam_want_stream && !g_cam_reconnect_req)
            {
                int r = recv(g_cam_socket, frame_payload + total_payload, payload_len - total_payload, 0);
                if (r < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        svcSleepThread(2 * 1000000LL); // 2ms sleep
                        continue;
                    }
                    err = 1;
                    break;
                }
                else if (r == 0)
                {
                    err = 1;
                    break;
                }
                total_payload += r;
            }

            if (err || !g_cam_want_stream || !g_cam_running || g_cam_reconnect_req || total_payload < payload_len)
            {
                if (g_cam_socket >= 0)
                {
                    shutdown(g_cam_socket, SHUT_RDWR);
                    close(g_cam_socket);
                    g_cam_socket = -1;
                }
                continue;
            }

            // Frame successfully received
            if (fmt == 0) // RGB565 raw full frame
            {
                graphics_cam_update_frame((const u16 *)frame_payload, w, h);
                g_cam_last_frame_time = osGetTime();
                frame_fps_counter++;
            }
            else if (fmt == 3) // Delta 8x8 Morton tiles
            {
                if (payload_len >= 2)
                {
                    u16 tile_count = (frame_payload[0] << 8) | frame_payload[1];
                    if (payload_len >= 2 + (u32)tile_count * 130)
                    {
                        graphics_cam_update_delta_tiles(frame_payload + 2, tile_count, w, h);
                        g_cam_last_frame_time = osGetTime();
                        frame_fps_counter++;
                    }
                }
            }
        }

        u64 cur_time = osGetTime();
        if (cur_time - last_fps_time >= 1000)
        {
            g_cam_fps = frame_fps_counter;
            frame_fps_counter = 0;
            last_fps_time = cur_time;
        }
    }

    if (g_cam_socket >= 0)
    {
        shutdown(g_cam_socket, SHUT_RDWR);
        close(g_cam_socket);
        g_cam_socket = -1;
    }
}

Result net_cam_start(void)
{
    g_cam_running = 1;
    g_cam_reconnect_req = 0;
    cam_thread = threadCreate(cam_thread_func, NULL, 65536, 0x31, -2, false);
    return cam_thread ? 0 : -1;
}

void net_cam_reconnect(void)
{
    // Cooperative reconnect: worker thread safely closes its own socket
    g_cam_reconnect_req = 1;
}

void net_cam_stop(void)
{
    g_cam_running = 0;
    g_cam_want_stream = 0;
    g_cam_reconnect_req = 1;
    if (cam_thread)
    {
        threadJoin(cam_thread, 1000000000ULL); // 1-second timeout
        threadFree(cam_thread);
        cam_thread = NULL;
    }
    if (g_cam_socket >= 0)
    {
        shutdown(g_cam_socket, SHUT_RDWR);
        close(g_cam_socket);
        g_cam_socket = -1;
    }
}

void network_cam_start(void)
{
    g_cam_want_stream = 1;
    g_stream_active = 1;
}

void network_cam_stop(void)
{
    g_cam_want_stream = 0;
    g_stream_active = 0;
    if (g_cam_socket >= 0)
    {
        shutdown(g_cam_socket, SHUT_RDWR);
        close(g_cam_socket);
        g_cam_socket = -1;
    }
}

void network_cam_send_cmd(char cmd)
{
    if (g_cam_socket >= 0)
    {
        send(g_cam_socket, &cmd, 1, 0);
    }
}
#endif
