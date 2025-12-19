#include "common.h"

// TODO WHAAAAAAAAT
typedef struct IPC_Interface {
    int (*init_server)(int port_or_id);
    int (*init_client)(const char* address, int port_or_id);
    int (*send_packet)(int target_fd, GamePacket *packet);
    int (*receive_packet)(int source_fd, GamePacket *packet);
    void (*close_conn)(int fd);
} IPC_Interface;


IPC_Interface get_socket_interface();
//IPC_Interface get_pipe_interface();