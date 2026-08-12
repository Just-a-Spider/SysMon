#include "input.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <3ds.h>

void prompt_for_ip(char *ip_buffer, int buffer_size)
{
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 1, -1);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetHintText(&swkbd, "Enter Server IP Address");
    swkbdSetInitialText(&swkbd, ip_buffer);

    SwkbdButton button = swkbdInputText(&swkbd, ip_buffer, buffer_size);
    if (button != SWKBD_BUTTON_CONFIRM)
    {
        // Keep existing if cancelled
    }
}

void prompt_for_port(int *port)
{
    SwkbdState swkbd;
    char port_buffer[10];
    snprintf(port_buffer, sizeof(port_buffer), "%d", *port);

    swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 1, -1);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetHintText(&swkbd, "Enter Server Port");
    swkbdSetInitialText(&swkbd, port_buffer);

    SwkbdButton button = swkbdInputText(&swkbd, port_buffer, sizeof(port_buffer));
    if (button == SWKBD_BUTTON_CONFIRM)
    {
        *port = atoi(port_buffer);
    }
}

void prompt_for_key(char *key_buffer, int buffer_size)
{
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 1, 8);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetHintText(&swkbd, "Enter 4-Digit Auth PIN");
    swkbdSetInitialText(&swkbd, key_buffer);

    SwkbdButton button = swkbdInputText(&swkbd, key_buffer, buffer_size);
    if (button != SWKBD_BUTTON_CONFIRM)
    {
        // Keep existing if cancelled
    }
}
