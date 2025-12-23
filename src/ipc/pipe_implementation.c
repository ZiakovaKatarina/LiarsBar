// src/ipc/pipe_implementation.c
#include "../../include/ipc_interface.h"
#include "../../include/common.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define SERVER_REG_FIFO "/tmp/liarsbar_server_reg.fifo"
#define FIFO_S2C "/tmp/liarsbar_s2c_%d.fifo"  // Server → Client
#define FIFO_C2S "/tmp/liarsbar_c2s_%d.fifo"  // Client → Server

typedef struct {
    pid_t pid;
    int c2s_fd;  // server číta od klienta
    int s2c_fd;  // server píše klientovi
} PipeClient;

static struct {
    PipeClient clients[MAX_PLAYERS];
    int num_clients;
    int reg_fd;
} ctx = { .num_clients = 0, .reg_fd = -1 };

static int p_init_server() {
    unlink(SERVER_REG_FIFO);

    if (mkfifo(SERVER_REG_FIFO, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo server_reg");
        return -1;
    }

    printf("[SERVER] Čakám na prvého klienta pre registráciu...\n");
    fflush(stdout);

    // Blokujúce otvorenie – čaká na prvého klienta
    ctx.reg_fd = open(SERVER_REG_FIFO, O_RDONLY);
    if (ctx.reg_fd < 0) {
        perror("open server_reg (blocking)");
        return -1;
    }

    printf("[SERVER] Prvý klient sa zaregistroval, prepínam na non-blocking\n");
    fflush(stdout);

    // Prepnutie na non-blocking pre ďalších klientov
    int flags = fcntl(ctx.reg_fd, F_GETFL);
    fcntl(ctx.reg_fd, F_SETFL, flags | O_NONBLOCK);

    ctx.num_clients = 0;
    memset(ctx.clients, 0, sizeof(ctx.clients));
    for (int i = 0; i < MAX_PLAYERS; i++) {
        ctx.clients[i].c2s_fd = -1;
        ctx.clients[i].s2c_fd = -1;
    }

    printf("Server spustený a čaká na hráčov...\n");
    fflush(stdout);

    return ctx.reg_fd;
}

static int p_accept_client(int server_fd) {
    (void)server_fd;

    pid_t pid;
    ssize_t r = read(ctx.reg_fd, &pid, sizeof(pid));

    if (r != sizeof(pid)) {
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return -1;
        }
        perror("read pid from reg_fifo");
        return -1;
    }

    printf("[SERVER] Nový klient sa zaregistroval s PID = %d\n", (int)pid);
    fflush(stdout);

    char s2c_path[64], c2s_path[64];
    snprintf(s2c_path, sizeof(s2c_path), FIFO_S2C, (int)pid);
    snprintf(c2s_path, sizeof(c2s_path), FIFO_C2S, (int)pid);

    PipeClient *c = &ctx.clients[ctx.num_clients];

    // Najprv C2S na čítanie (klient už má otvorené na zápis)
    printf("[SERVER] Otváram C2S na čítanie ako prvý: %s\n", c2s_path);
    fflush(stdout);

    c->c2s_fd = open(c2s_path, O_RDONLY | O_NONBLOCK);
    if (c->c2s_fd < 0) {
        perror("open c2s_fifo for reading");
        return -1;
    }

    printf("[SERVER] C2S otvorené (fd=%d)\n", c->c2s_fd);
    fflush(stdout);

    // Potom S2C na zápis
    printf("[SERVER] Otváram S2C na zápis: %s\n", s2c_path);
    fflush(stdout);

    c->s2c_fd = open(s2c_path, O_WRONLY | O_NONBLOCK);
    if (c->s2c_fd < 0) {
        perror("open s2c_fifo for writing");
        close(c->c2s_fd);
        c->c2s_fd = -1;
        return -1;
    }

    printf("[SERVER] S2C otvorené (fd=%d)\n", c->s2c_fd);
    fflush(stdout);

    c->pid = pid;
    ctx.num_clients++;

    printf("[SERVER] Klient PID %d úspešne pripojený! Vraciam c2s_fd = %d\n", (int)pid, c->c2s_fd);
    fflush(stdout);

    return c->c2s_fd;
}

static int p_init_client(const char *unused) {
    (void)unused;

    pid_t pid = getpid();
    printf("[CLIENT] === ŠTART PRIPOJENIA PID %d ===\n", (int)pid);
    fflush(stdout);

    char s2c_path[64], c2s_path[64];
    snprintf(s2c_path, sizeof(s2c_path), FIFO_S2C, (int)pid);
    snprintf(c2s_path, sizeof(c2s_path), FIFO_C2S, (int)pid);

    unlink(s2c_path);
    unlink(c2s_path);

    if (mkfifo(s2c_path, 0666) < 0 || mkfifo(c2s_path, 0666) < 0) {
        perror("[CLIENT] mkfifo");
        goto cleanup;
    }

    printf("[CLIENT] FIFO vytvorené: %s a %s\n", s2c_path, c2s_path);
    fflush(stdout);

    int reg_fd = open(SERVER_REG_FIFO, O_WRONLY);
    if (reg_fd < 0) {
        perror("[CLIENT] open reg_fifo");
        goto cleanup;
    }

    if (write(reg_fd, &pid, sizeof(pid)) != sizeof(pid)) {
        perror("[CLIENT] write pid");
        close(reg_fd);
        goto cleanup;
    }

    printf("[CLIENT] PID odoslaný (reg_fd nechávam otvorený)\n");
    fflush(stdout);

    // Dummy open proti ENXIO
    int dummy_c2s = open(c2s_path, O_RDONLY | O_NONBLOCK);
    int dummy_s2c = open(s2c_path, O_WRONLY | O_NONBLOCK);
    printf("[CLIENT] Dummy open vykonaný (na zabránenie ENXIO)\n");
    fflush(stdout);

    // Najprv C2S na zápis
    int c2s_fd = open(c2s_path, O_WRONLY | O_NONBLOCK);
    if (c2s_fd < 0) {
        perror("[CLIENT] open c2s for writing (final)");
        if (dummy_c2s >= 0) close(dummy_c2s);
        if (dummy_s2c >= 0) close(dummy_s2c);
        close(reg_fd);
        goto cleanup;
    }

    // Potom S2C na čítanie
    int s2c_fd = open(s2c_path, O_RDONLY | O_NONBLOCK);
    if (s2c_fd < 0) {
        perror("[CLIENT] open s2c for reading (final)");
        close(c2s_fd);
        if (dummy_c2s >= 0) close(dummy_c2s);
        if (dummy_s2c >= 0) close(dummy_s2c);
        close(reg_fd);
        goto cleanup;
    }

    if (dummy_c2s >= 0) close(dummy_c2s);
    if (dummy_s2c >= 0) close(dummy_s2c);

    printf("[CLIENT] === ÚSPEŠNÉ PRIPOJENIE! s2c_fd = %d ===\n", s2c_fd);
    fflush(stdout);

    return s2c_fd;

cleanup:
    unlink(s2c_path);
    unlink(c2s_path);
    return -1;
}

static int p_send_packet(int fd, GamePacket *packet) {
    size_t left = sizeof(GamePacket);
    const char *ptr = (const char *)packet;

    while (left > 0) {
        ssize_t w = write(fd, ptr, left);
        if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        if (w == 0) return -1;
        ptr += w;
        left -= w;
    }
    return sizeof(GamePacket);
}

static int p_receive_packet(int fd, GamePacket *packet) {
    size_t left = sizeof(GamePacket);
    char *ptr = (char *)packet;

    while (left > 0) {
        ssize_t r = read(fd, ptr, left);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        if (r == 0) return -1;
        ptr += r;
        left -= r;
    }
    return sizeof(GamePacket);
}

static void p_close_conn(int fd) {
    if (fd < 0) return;

    close(fd);

    for (int i = 0; i < ctx.num_clients; i++) {
        PipeClient *c = &ctx.clients[i];
        if (c->c2s_fd == fd || c->s2c_fd == fd) {
            if (c->c2s_fd >= 0) close(c->c2s_fd);
            if (c->s2c_fd >= 0) close(c->s2c_fd);

            char s2c_path[64], c2s_path[64];
            snprintf(s2c_path, sizeof(s2c_path), FIFO_S2C, (int)c->pid);
            snprintf(c2s_path, sizeof(c2s_path), FIFO_C2S, (int)c->pid);
            unlink(s2c_path);
            unlink(c2s_path);

            printf("[SERVER] Klient PID %d odpojený – FIFO vyčistené\n", (int)c->pid);
            fflush(stdout);

            ctx.clients[i] = ctx.clients[--ctx.num_clients];
            ctx.clients[ctx.num_clients].c2s_fd = -1;
            ctx.clients[ctx.num_clients].s2c_fd = -1;
            return;
        }
    }

    if (fd == ctx.reg_fd) {
        unlink(SERVER_REG_FIFO);
        ctx.reg_fd = -1;
    }
}

static int p_get_write_fd(int read_fd) {
    for (int i = 0; i < ctx.num_clients; i++) {
        if (ctx.clients[i].c2s_fd == read_fd) {
            return ctx.clients[i].s2c_fd;
        }
    }
    return -1;  // nemal by sa stať
}

IPC_Interface get_pipe_interface() {
    IPC_Interface iface = {
        .init_server = p_init_server,
        .init_client = p_init_client,
        .send_packet = p_send_packet,
        .receive_packet = p_receive_packet,
        .close_conn = p_close_conn,
        .accept_client = p_accept_client,
        .get_write_fd = p_get_write_fd
    };
    return iface;
}