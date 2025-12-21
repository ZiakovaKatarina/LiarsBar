#ifndef IPC_INTERFACE_H
#define IPC_INTERFACE_H

#include "common.h"

typedef struct IPC_Interface {
    int (*init_server)();                    // bez parametra
    int (*init_client)(const char* address); // len adresa
    int (*send_packet)(int target_fd, GamePacket *packet);
    int (*receive_packet)(int source_fd, GamePacket *packet);
    void (*close_conn)(int fd);
} IPC_Interface;

IPC_Interface get_socket_interface();

#endif