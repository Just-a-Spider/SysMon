#ifndef NET_CTRL_H
#define NET_CTRL_H

#include <3ds.h>

extern int g_ctrl_active;
extern int g_ctrl_port;
extern int g_ctrl_packet_rate;
extern int g_ctrl_physical_map;

Result net_ctrl_start(const char *ip, int port);
void   net_ctrl_stop(void);
void   net_ctrl_send_tick(u32 buttons, s16 cx, s16 cy, s16 rx, s16 ry, u32 flags);
int    net_ctrl_is_active(void);

#endif
