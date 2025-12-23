#include "../../include/ipc_interface.h"
#include "../../include/common.h"

#include <errno.h>

static int s_init_server() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(PORT) };
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return -1;
    listen(fd, MAX_PLAYERS);
    return fd;
}

static int s_init_client(const char* addr_str) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(PORT) };
    inet_pton(AF_INET, addr_str, &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return -1;
    return fd;
}

static int s_send(int fd, GamePacket *p) { return send(fd, p, sizeof(GamePacket), 0); }
static int s_recv(int fd, GamePacket *p) { return recv(fd, p, sizeof(GamePacket), 0); }
static void s_close(int fd) { close(fd); }

static int s_accept_client(int server_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1;  // žiadny čakajúci klient (non-blocking)
        }
        perror("accept");
    }
    // Voliteľne: nastaviť non-blocking aj na client_fd
    // fcntl(client_fd, F_SETFL, O_NONBLOCK);
    return client_fd;
}

static int s_get_write_fd(int read_fd) {
    return read_fd;  // sockets sú bidirekcionálne
}

IPC_Interface get_socket_interface() {
    IPC_Interface iface = {
        .init_server = s_init_server,
        .init_client = s_init_client,
        .send_packet = s_send,
        .receive_packet = s_recv,
        .close_conn = s_close,
        .accept_client = s_accept_client,
        .get_write_fd = s_get_write_fd
    };
    return iface;
}