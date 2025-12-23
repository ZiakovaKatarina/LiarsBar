#ifndef IPC_INTERFACE_H
#define IPC_INTERFACE_H

#include "common.h"

typedef struct IPC_Interface {
    int (*init_server)();
    int (*init_client)(const char* address);
    int (*send_packet)(int target_fd, GamePacket *packet);
    int (*receive_packet)(int source_fd, GamePacket *packet);
    void (*close_conn)(int fd);
    int (*accept_client)(int server_fd);
    int (*get_write_fd)(int read_fd);
} IPC_Interface;

IPC_Interface get_socket_interface();
IPC_Interface get_pipe_interface();

#endif