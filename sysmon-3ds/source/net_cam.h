#ifndef NET_CAM_H
#define NET_CAM_H

#include <3ds.h>

#ifndef DISABLE_CAM
extern int g_stream_port;
extern int g_stream_active;
extern int g_cam_top_screen;
extern int g_cam_zoom_mode;
extern int g_cam_monitor_idx;
extern int g_cam_fps;
extern char g_cam_source_name[32];
extern u64 g_cam_last_frame_time;

Result net_cam_start(void);
void   net_cam_stop(void);
void   net_cam_reconnect(void);
void   network_cam_start(void);
void   network_cam_stop(void);
void   network_cam_send_cmd(char cmd);
#endif

#endif
