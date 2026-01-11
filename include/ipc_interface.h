#ifndef IPC_INTERFACE_H
#define IPC_INTERFACE_H

#include "common.h"

typedef struct {
    int (*init_server)(int port);
    int (*init_client)(const char* address, int port);
    int (*send_packet)(int fd, const GamePacket* packet);
    int (*receive_packet)(int fd, GamePacket* packet);
    int (*accept_client)(int server_fd);
    void (*close_conn)(int fd);
} IPC_Interface;

IPC_Interface get_socket_interface();

#endif
