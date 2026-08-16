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

void network_exit(void)
{
    net_ctrl_stop();
#ifndef DISABLE_CAM
    net_cam_stop();
#endif
    net_telemetry_stop();
    net_core_exit();
}
