#include "network.h"

Result network_init(const char *ip, int port)
{
    Result res = net_core_init(ip, port);
    if (R_FAILED(res))
        return res;

    res = net_telemetry_start();
    if (R_FAILED(res))
        return res;

#ifndef DISABLE_CAM
    res = net_cam_start();
    if (R_FAILED(res))
        return res;
#endif

    return 0;
}

void network_switch_profile(int profile_idx)
{
    if (profile_idx < 0 || profile_idx >= g_profile_count)
        return;

    g_profile_index = profile_idx;
    network_save_profiles();

    net_core_set_target(g_profiles[g_profile_index].ip,
                        g_profiles[g_profile_index].port,
                        g_profiles[g_profile_index].pin);

    net_ctrl_stop();
    net_telemetry_reconnect();
#ifndef DISABLE_CAM
    net_cam_reconnect();
#endif
}

void network_exit(void)
{
    net_ctrl_stop();
#ifndef DISABLE_CAM
    net_cam_stop();
#endif
    net_telemetry_stop();
    net_core_exit();
}
