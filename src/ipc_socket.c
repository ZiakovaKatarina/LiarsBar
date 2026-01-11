#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/ipc_interface.h"

static int s_init_server() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("Socket creation failed");
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_addr.s_addr = INADDR_ANY,
                               .sin_port = htons(PORT)};

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(fd);
        return -1;
    }

    if (listen(fd, MAX_GAMES) < 0) {
        perror("Listen failed");
        close(fd);
        return -1;
    }

    return fd;
}

static int s_init_client(const char* addr_str) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("Socket creation failed");
        return -1;
    }

    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(PORT)};

    if (inet_pton(AF_INET, addr_str, &addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int s_accept_client(int server_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd =
        accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("Accept failed");
        }
        return -1;
    }
    return client_fd;
}

static int s_send(int fd, const GamePacket* p) {
    size_t total = sizeof(GamePacket);
    size_t sent = 0;
    const char* ptr = (const char*)p;

    while (sent < total) {
        ssize_t res = send(fd, ptr + sent, total - sent, 0);
        if (res <= 0) {
            if (res < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            }
            return -1;
        }
        sent += res;
    }
    return (int)sent;
}

static int s_recv(int fd, GamePacket* p) {
    size_t total = sizeof(GamePacket);
    size_t received = 0;
    char* ptr = (char*)p;

    while (received < total) {
        ssize_t res = recv(fd, ptr + received, total - received, 0);
        if (res <= 0) {
            if (res == 0) return 0;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        received += res;
    }
    return (int)received;
}

static void s_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

IPC_Interface get_socket_interface() {
    IPC_Interface iface = {.init_server = s_init_server,
                           .init_client = s_init_client,
                           .accept_client = s_accept_client,
                           .send_packet = s_send,
                           .receive_packet = s_recv,
                           .close_conn = s_close};
    return iface;
}
