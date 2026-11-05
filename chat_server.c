//315581038 黃永恩

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PORT 5555
#define MAX_CLIENTS 64
#define MAX_NAME 32
#define BUF_SIZE 1024
#define UDP_TIMEOUT 90

#define TCP 1
#define UDP 2

struct client {
    int used;
    int type;                   // TCP or UDP
    int fd;                     // only for TCP
    struct sockaddr_in addr;    // only for UDP
    char name[MAX_NAME];        // empty before login
    char buf[BUF_SIZE];         // TCP input buffer
    int buf_len;
    time_t last_seen;
};

struct client clients[MAX_CLIENTS];
int udp_fd = -1;
int running = 1;

void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

// send a message to client i
void send_to(int i, const char *msg) {
    int len = strlen(msg);
    if (clients[i].type == TCP) {
        send(clients[i].fd, msg, len, 0);
    } else {
        sendto(udp_fd, msg, len, 0, (struct sockaddr *)&clients[i].addr,
               sizeof(clients[i].addr));
    }
}

// send a message to everyone who has logged in (except one)
void broadcast(const char *msg, int except) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (i != except && clients[i].used && clients[i].name[0] != '\0')
            send_to(i, msg);
    }
}

void remove_client(int i) {
    char name[MAX_NAME];
    strcpy(name, clients[i].name);

    if (clients[i].type == TCP)
        close(clients[i].fd);
    memset(&clients[i], 0, sizeof(clients[i]));

    if (name[0] != '\0') {
        char msg[BUF_SIZE];
        snprintf(msg, sizeof(msg), "[system] %s left\n", name);
        broadcast(msg, -1);
        printf("%s left\n", name);
    }
}

int find_free_slot(void) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (!clients[i].used)
            return i;
    return -1;
}

int find_by_name(const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].used && strcmp(clients[i].name, name) == 0)
            return i;
    return -1;
}

int find_by_addr(struct sockaddr_in *addr) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].used && clients[i].type == UDP &&
            clients[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            clients[i].addr.sin_port == addr->sin_port)
            return i;
    }
    return -1;
}

// name can only have letters, digits, '_' and '-'
int valid_name(const char *name) {
    int len = strlen(name);
    if (len == 0 || len >= MAX_NAME)
        return 0;
    for (int i = 0; i < len; i++) {
        char c = name[i];
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

void list_users(int i) {
    char msg[4096] = "[users]";
    for (int j = 0; j < MAX_CLIENTS; j++) {
        if (clients[j].used && clients[j].name[0] != '\0') {
            char item[64];
            snprintf(item, sizeof(item), " %s(%s)", clients[j].name,
                     clients[j].type == TCP ? "tcp" : "udp");
            strcat(msg, item);
        }
    }
    strcat(msg, "\n");
    send_to(i, msg);
}

// handle one line (without '\n') from client i
void handle_line(int i, char *line) {
    char msg[BUF_SIZE + 100];
    clients[i].last_seen = time(NULL);

    // remove '\r' at the end (in case of Windows style line)
    int len = strlen(line);
    if (len > 0 && line[len - 1] == '\r')
        line[len - 1] = '\0';

    // not logged in yet: must be HELLO <name>
    if (clients[i].name[0] == '\0') {
        if (strncmp(line, "HELLO ", 6) != 0 || !valid_name(line + 6)) {
            send_to(i, "ERROR Expected HELLO <name>\n");
            remove_client(i);
            return;
        }
        if (find_by_name(line + 6) >= 0) {
            send_to(i, "ERROR Username already in use\n");
            remove_client(i);
            return;
        }
        strcpy(clients[i].name, line + 6);
        const char *proto = clients[i].type == TCP ? "TCP" : "UDP";
        snprintf(msg, sizeof(msg), "WELCOME %s\n", proto);
        send_to(i, msg);
        snprintf(msg, sizeof(msg), "[system] %s joined (%s)\n", clients[i].name, proto);
        broadcast(msg, i);
        printf("%s joined via %s\n", clients[i].name, proto);
        return;
    }

    if (strcmp(line, "PING") == 0) {
        return;
    } else if (strcmp(line, "QUIT") == 0) {
        send_to(i, "BYE\n");
        remove_client(i);
    } else if (strcmp(line, "LIST") == 0) {
        list_users(i);
    } else if (strncmp(line, "MSG ", 4) == 0) {
        snprintf(msg, sizeof(msg), "[%s] %s\n", clients[i].name, line + 4);
        broadcast(msg, -1);
    } else if (strncmp(line, "PM ", 3) == 0) {
        // format: PM <target> <text>
        char *target = line + 3;
        char *text = strchr(target, ' ');
        if (text == NULL) {
            send_to(i, "ERROR Usage: /msg <name> <message>\n");
            return;
        }
        *text = '\0';
        text++;

        int dest = find_by_name(target);
        if (dest < 0) {
            send_to(i, "ERROR User not found\n");
        } else if (dest == i) {
            send_to(i, "ERROR Cannot send a private message to yourself\n");
        } else {
            snprintf(msg, sizeof(msg), "[PM from %s] %s\n", clients[i].name, text);
            send_to(dest, msg);
            snprintf(msg, sizeof(msg), "[PM to %s] %s\n", target, text);
            send_to(i, msg);
        }
    } else {
        send_to(i, "ERROR Unknown command\n");
    }
}

// read data from a TCP client, split it into lines
void handle_tcp(int i) {
    char data[BUF_SIZE];
    int n = recv(clients[i].fd, data, sizeof(data), 0);
    if (n <= 0) {
        remove_client(i);
        return;
    }
    for (int k = 0; k < n; k++) {
        if (data[k] == '\n') {
            clients[i].buf[clients[i].buf_len] = '\0';
            char line[BUF_SIZE];
            strcpy(line, clients[i].buf);
            clients[i].buf_len = 0;
            handle_line(i, line);
            if (!clients[i].used)   // client may have quit
                return;
        } else if (clients[i].buf_len < BUF_SIZE - 1) {
            clients[i].buf[clients[i].buf_len++] = data[k];
        } else {
            send_to(i, "ERROR Line too long\n");
            remove_client(i);
            return;
        }
    }
}

// read one UDP packet (one packet = one line)
void handle_udp(void) {
    char line[BUF_SIZE];
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int n = recvfrom(udp_fd, line, sizeof(line) - 1, 0, (struct sockaddr *)&addr, &addr_len);
    if (n <= 0)
        return;
    line[n] = '\0';
    if (n > 0 && line[n - 1] == '\n')
        line[n - 1] = '\0';

    int i = find_by_addr(&addr);
    if (i < 0) {
        // new UDP user, must start with HELLO
        if (strncmp(line, "HELLO ", 6) != 0)
            return;
        i = find_free_slot();
        if (i < 0)
            return;
        clients[i].used = 1;
        clients[i].type = UDP;
        clients[i].fd = -1;
        clients[i].addr = addr;
    } else if (strncmp(line, "HELLO ", 6) == 0) {
        // client resent HELLO because our WELCOME was lost
        clients[i].last_seen = time(NULL);
        send_to(i, "WELCOME UDP\n");
        return;
    }
    handle_line(i, line);
}

int create_socket(int port, int type) {
    int fd = socket(AF_INET, type, 0);
    if (fd < 0)
        return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (type == SOCK_STREAM && listen(fd, 10) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    char *mode = "both";

    if (argc > 3) {
        printf("Usage: %s [port] [tcp|udp|both]\n", argv[0]);
        return 1;
    }
    if (argc >= 2)
        port = atoi(argv[1]);
    if (argc >= 3)
        mode = argv[2];
    if (port <= 0 || port > 65535) {
        printf("Invalid port\n");
        return 1;
    }
    if (strcmp(mode, "tcp") != 0 && strcmp(mode, "udp") != 0 && strcmp(mode, "both") != 0) {
        printf("Mode must be tcp, udp or both\n");
        return 1;
    }

    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN);

    int use_tcp = strcmp(mode, "udp") != 0;
    int use_udp = strcmp(mode, "tcp") != 0;
    int tcp_fd = -1;

    if (use_tcp) {
        tcp_fd = create_socket(port, SOCK_STREAM);
        if (tcp_fd < 0) {
            perror("TCP socket");
            return 1;
        }
    }
    if (use_udp) {
        udp_fd = create_socket(port, SOCK_DGRAM);
        if (udp_fd < 0) {
            perror("UDP socket");
            return 1;
        }
    }
    printf("Server listening on port %d (%s). Ctrl+C to stop.\n", port, mode);
    fflush(stdout);

    while (running) {
        // build the poll list: [tcp listener] [udp socket] [tcp clients...]
        struct pollfd fds[MAX_CLIENTS + 2];
        int owner[MAX_CLIENTS + 2];   // which client each entry belongs to
        int count = 0;

        if (tcp_fd >= 0) {
            fds[count].fd = tcp_fd;
            fds[count].events = POLLIN;
            owner[count++] = -1;
        }
        if (udp_fd >= 0) {
            fds[count].fd = udp_fd;
            fds[count].events = POLLIN;
            owner[count++] = -2;
        }
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].used && clients[i].type == TCP) {
                fds[count].fd = clients[i].fd;
                fds[count].events = POLLIN;
                owner[count++] = i;
            }
        }

        int ready = poll(fds, count, 1000);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }

        for (int j = 0; j < count; j++) {
            if (fds[j].revents == 0)
                continue;

            if (owner[j] == -1) {
                // new TCP connection
                int fd = accept(tcp_fd, NULL, NULL);
                if (fd < 0)
                    continue;
                int i = find_free_slot();
                if (i < 0) {
                    close(fd);
                    continue;
                }
                clients[i].used = 1;
                clients[i].type = TCP;
                clients[i].fd = fd;
            } else if (owner[j] == -2) {
                handle_udp();
            } else {
                int i = owner[j];
                if (clients[i].used && clients[i].fd == fds[j].fd)
                    handle_tcp(i);
            }
        }

        // remove UDP users that have been silent too long
        time_t now = time(NULL);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].used && clients[i].type == UDP &&
                now - clients[i].last_seen > UDP_TIMEOUT)
                remove_client(i);
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].used && clients[i].type == TCP)
            close(clients[i].fd);
    if (tcp_fd >= 0)
        close(tcp_fd);
    if (udp_fd >= 0)
        close(udp_fd);
    printf("Server stopped.\n");
    return 0;
}
