#ifndef NETWORK_H
#define NETWORK_H

#include <3ds.h>
#include "net_core.h"
#include "net_telemetry.h"

#include "net_ctrl.h"

#ifndef DISABLE_CAM
#include "net_cam.h"
#endif

// ---------------------------------------------------------------------------
// Network Facade API
// ---------------------------------------------------------------------------
Result network_init(const char *ip, int port);
void   network_switch_profile(int profile_idx);
void   network_exit(void);

#endif
