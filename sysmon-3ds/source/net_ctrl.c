#include "net_ctrl.h"
#include "net_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <3ds/os.h>

int g_ctrl_active = 0;
int g_ctrl_port = 7339;
int g_ctrl_packet_rate = 0;
int g_ctrl_physical_map = 1; // 1 = Physical position (PSP style), 0 = Nintendo letter

static int s_ctrl_sock = -1;
static struct sockaddr_in s_srv_addr;
static u32 s_seq = 0;
static u64 s_last_rate_time = 0;
static int s_packets_this_sec = 0;

Result net_ctrl_start(const char *ip, int port)
{
    if (s_ctrl_sock >= 0)
    {
        close(s_ctrl_sock);
        s_ctrl_sock = -1;
    }

    s_ctrl_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_ctrl_sock < 0)
        return -1;

    // Set non-blocking so UDP sends never stall frame rendering
    int flags = fcntl(s_ctrl_sock, F_GETFL, 0);
    if (flags != -1)
        fcntl(s_ctrl_sock, F_SETFL, flags | O_NONBLOCK);

    memset(&s_srv_addr, 0, sizeof(s_srv_addr));
    s_srv_addr.sin_family = AF_INET;
    s_srv_addr.sin_port = htons(port > 0 ? port : g_ctrl_port);
    inet_pton(AF_INET, ip, &s_srv_addr.sin_addr);

    g_ctrl_active = 1;
    s_seq = 0;
    s_last_rate_time = osGetTime();
    s_packets_this_sec = 0;

    return 0;
}

void net_ctrl_stop(void)
{
    g_ctrl_active = 0;
    if (s_ctrl_sock >= 0)
    {
        close(s_ctrl_sock);
        s_ctrl_sock = -1;
    }
}

int net_ctrl_is_active(void)
{
    return g_ctrl_active && (s_ctrl_sock >= 0);
}

void net_ctrl_send_tick(u32 buttons, s16 cx, s16 cy, s16 rx, s16 ry, u32 flags)
{
    if (s_ctrl_sock < 0)
        return;

    u8 pkt[28];
    // Magic: "3PAD"
    pkt[0] = '3';
    pkt[1] = 'P';
    pkt[2] = 'A';
    pkt[3] = 'D';

    // Sequence (Big Endian)
    s_seq++;
    pkt[4] = (u8)((s_seq >> 24) & 0xFF);
    pkt[5] = (u8)((s_seq >> 16) & 0xFF);
    pkt[6] = (u8)((s_seq >> 8)  & 0xFF);
    pkt[7] = (u8)(s_seq & 0xFF);

    // PIN Authentication Token (Big Endian)
    u32 pin_val = (u32)strtoul(g_auth_key, NULL, 10);
    pkt[8]  = (u8)((pin_val >> 24) & 0xFF);
    pkt[9]  = (u8)((pin_val >> 16) & 0xFF);
    pkt[10] = (u8)((pin_val >> 8)  & 0xFF);
    pkt[11] = (u8)(pin_val & 0xFF);

    // Buttons bitmask (Big Endian)
    pkt[12] = (u8)((buttons >> 24) & 0xFF);
    pkt[13] = (u8)((buttons >> 16) & 0xFF);
    pkt[14] = (u8)((buttons >> 8)  & 0xFF);
    pkt[15] = (u8)(buttons & 0xFF);

    // Circle Pad X / Y (Big Endian)
    pkt[16] = (u8)((cx >> 8) & 0xFF);
    pkt[17] = (u8)(cx & 0xFF);
    pkt[18] = (u8)((cy >> 8) & 0xFF);
    pkt[19] = (u8)(cy & 0xFF);

    // Right Stick X / Y (Big Endian)
    pkt[20] = (u8)((rx >> 8) & 0xFF);
    pkt[21] = (u8)(rx & 0xFF);
    pkt[22] = (u8)((ry >> 8) & 0xFF);
    pkt[23] = (u8)(ry & 0xFF);

    // Flags (Big Endian) - bit 2 (0x04) = physical mapping
    u32 final_flags = flags;
    if (g_ctrl_physical_map)
        final_flags |= 0x04;

    pkt[24] = (u8)((final_flags >> 24) & 0xFF);
    pkt[25] = (u8)((final_flags >> 16) & 0xFF);
    pkt[26] = (u8)((final_flags >> 8)  & 0xFF);
    pkt[27] = (u8)(final_flags & 0xFF);

    ssize_t sent = sendto(s_ctrl_sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&s_srv_addr, sizeof(s_srv_addr));

    // Rate counter calculation (only increment if packet went to network interface)
    if (sent > 0)
        s_packets_this_sec++;

    u64 now = osGetTime();
    if (now - s_last_rate_time >= 1000)
    {
        g_ctrl_packet_rate = (int)((u64)s_packets_this_sec * 1000 / (now - s_last_rate_time));
        s_packets_this_sec = 0;
        s_last_rate_time = now;
    }
}
