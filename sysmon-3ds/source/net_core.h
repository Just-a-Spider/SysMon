#ifndef NET_CORE_H
#define NET_CORE_H

#include <3ds.h>

#define MAX_SERVER_PROFILES 8

typedef struct
{
    char name[16];
    char ip[60];
    int  port;
    char pin[16];
} ServerProfile;

extern ServerProfile g_profiles[MAX_SERVER_PROFILES];
extern int g_profile_count;
extern int g_profile_index;
extern char g_auth_key[16];
extern char g_srv_ip[64];
extern int g_srv_port;

Result net_core_init(const char *ip, int port);
void   net_core_exit(void);
void   network_load_profiles(void);
void   network_save_profiles(void);

#endif
