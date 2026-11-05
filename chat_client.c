//315581038 黃永恩

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
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
#define BUF_SIZE 1024
#define TIMEOUT_MS 2000

int running = 1;

void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

int valid_name(const char *name) {
    int len = strlen(name);
    if (len == 0 || len > 31)
        return 0;
    for (int i = 0; i < len; i++) {
        char c = name[i];
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

// turn hostname (e.g. "127.0.0.1" or "localhost") into an address
int get_address(const char *host, int port, struct sockaddr_in *addr) {
    struct addrinfo hints, *res;
    char port_str[16];
    sprintf(port_str, "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        printf("Cannot resolve host %s\n", host);
        return -1;
    }
    memcpy(addr, res->ai_addr, sizeof(*addr));
    freeaddrinfo(res);
    return 0;
}

// read one line from TCP, give up after timeout
int read_line(int fd, char *out, int size, int timeout_ms) {
    int len = 0;
    while (len < size - 1) {
        struct pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, timeout_ms) <= 0)
            return -1;
        char c;
        if (recv(fd, &c, 1, 0) <= 0)
            return -1;
        if (c == '\n') {
            out[len] = '\0';
            return 0;
        }
        out[len++] = c;
    }
    return -1;
}

// try to connect with TCP, return socket or -1
int try_tcp(struct sockaddr_in *addr, const char *name) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    // use non-blocking connect so we can set a timeout
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (connect(fd, (struct sockaddr *)addr, sizeof(*addr)) < 0) {
        if (errno != EINPROGRESS) {
            close(fd);
            return -1;
        }
        struct pollfd p = {fd, POLLOUT, 0};
        int err = 0;
        socklen_t len = sizeof(err);
        if (poll(&p, 1, TIMEOUT_MS) <= 0 ||
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
            close(fd);
            return -1;
        }
    }
    fcntl(fd, F_SETFL, flags);  // back to blocking

    // say hello and wait for WELCOME TCP
    char msg[BUF_SIZE];
    snprintf(msg, sizeof(msg), "HELLO %s\n", name);
    send(fd, msg, strlen(msg), 0);
    if (read_line(fd, msg, sizeof(msg), TIMEOUT_MS) < 0) {
        close(fd);
        return -1;
    }
    if (strcmp(msg, "WELCOME TCP") != 0) {
        printf("Server: %s\n", msg);
        close(fd);
        return -1;
    }
    return fd;
}

// try to connect with UDP, return socket or -1
int try_udp(struct sockaddr_in *addr, const char *name) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    // UDP connect only remembers the server address, it doesn't check anything
    connect(fd, (struct sockaddr *)addr, sizeof(*addr));

    char hello[BUF_SIZE];
    snprintf(hello, sizeof(hello), "HELLO %s\n", name);

    // UDP may lose packets, so try 3 times
    for (int i = 0; i < 3; i++) {
        send(fd, hello, strlen(hello), 0);

        struct pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, 1000) <= 0)
            continue;

        char reply[BUF_SIZE];
        int n = recv(fd, reply, sizeof(reply) - 1, 0);
        if (n <= 0)
            continue;
        reply[n] = '\0';
        if (reply[n - 1] == '\n')
            reply[n - 1] = '\0';

        if (strcmp(reply, "WELCOME UDP") == 0)
            return fd;
        if (strncmp(reply, "ERROR", 5) == 0) {
            printf("Server: %s\n", reply);
            break;
        }
    }
    close(fd);
    return -1;
}

// print a message from server without messing up the "> " prompt
void print_message(const char *msg) {
    printf("\r%s\n> ", msg);
    fflush(stdout);
}

void chat(int fd, int is_tcp) {
    char buf[BUF_SIZE];     // for TCP: holds an unfinished line
    int buf_len = 0;
    time_t last_ping = time(NULL);

    printf("Commands: /msg <name> <text> | /list | /quit | /help\n> ");
    fflush(stdout);

    while (running) {
        // wait for keyboard input or server message
        struct pollfd fds[2];
        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[1].fd = fd;
        fds[1].events = POLLIN;

        int ready = poll(fds, 2, 1000);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        // message from server
        if (fds[1].revents) {
            char data[BUF_SIZE];
            int n = recv(fd, data, sizeof(data) - 1, 0);
            if (n <= 0) {
                printf("\nServer disconnected.\n");
                break;
            }
            if (is_tcp) {
                // TCP: data may contain several lines or half a line
                for (int k = 0; k < n; k++) {
                    if (data[k] == '\n') {
                        buf[buf_len] = '\0';
                        buf_len = 0;
                        if (strcmp(buf, "BYE") == 0)
                            return;
                        print_message(buf);
                    } else if (buf_len < BUF_SIZE - 1) {
                        buf[buf_len++] = data[k];
                    }
                }
            } else {
                // UDP: one packet = one line
                data[n] = '\0';
                if (data[n - 1] == '\n')
                    data[n - 1] = '\0';
                if (strcmp(data, "BYE") == 0)
                    return;
                print_message(data);
            }
        }

        // user typed something
        if (fds[0].revents & POLLIN) {
            char line[BUF_SIZE];
            char out[BUF_SIZE + 8];

            if (fgets(line, sizeof(line), stdin) == NULL)
                break;
            line[strcspn(line, "\n")] = '\0';

            if (strlen(line) == 0) {
                // empty line, do nothing
                printf("> ");
                fflush(stdout);
                continue;
            } else if (strcmp(line, "/quit") == 0) {
                break;
            } else if (strcmp(line, "/help") == 0) {
                printf("Type text to broadcast, /msg Alice hello, /list, or /quit.\n> ");
                fflush(stdout);
                continue;
            } else if (strcmp(line, "/list") == 0) {
                strcpy(out, "LIST\n");
            } else if (strncmp(line, "/msg ", 5) == 0) {
                snprintf(out, sizeof(out), "PM %s\n", line + 5);
            } else if (line[0] == '/') {
                printf("Unknown command; type /help.\n> ");
                fflush(stdout);
                continue;
            } else {
                snprintf(out, sizeof(out), "MSG %s\n", line);
            }

            if (send(fd, out, strlen(out), 0) < 0) {
                perror("send");
                break;
            }
            printf("> ");
            fflush(stdout);
        }

        // UDP: send PING every 20 seconds so server knows we're still here
        if (!is_tcp && time(NULL) - last_ping >= 20) {
            send(fd, "PING\n", 5, 0);
            last_ping = time(NULL);
        }
    }

    // tell server we are leaving
    send(fd, "QUIT\n", 5, 0);
}

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 5) {
        printf("Usage: %s <server> <your_name> [port] [tcp|udp|auto]\n", argv[0]);
        return 1;
    }

    char *host = argv[1];
    char *name = argv[2];
    int port = DEFAULT_PORT;
    char *mode = "auto";
    if (argc >= 4)
        port = atoi(argv[3]);
    if (argc >= 5)
        mode = argv[4];

    if (!valid_name(name)) {
        printf("Username: 1-31 letters, digits, underscores or hyphens\n");
        return 1;
    }
    if (port <= 0 || port > 65535) {
        printf("Invalid port\n");
        return 1;
    }
    if (strcmp(mode, "tcp") != 0 && strcmp(mode, "udp") != 0 && strcmp(mode, "auto") != 0) {
        printf("Protocol must be tcp, udp or auto\n");
        return 1;
    }

    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IONBF, 0);

    struct sockaddr_in addr;
    if (get_address(host, port, &addr) < 0)
        return 1;

    int fd = -1;
    int is_tcp = 0;

    // tcp or auto: try TCP first
    if (strcmp(mode, "udp") != 0) {
        printf("Trying TCP...\n");
        fd = try_tcp(&addr, name);
        if (fd >= 0) {
            printf("Connected via TCP.\n");
            is_tcp = 1;
        }
    }
    // udp, or auto and TCP failed: try UDP
    if (fd < 0 && strcmp(mode, "tcp") != 0) {
        if (strcmp(mode, "auto") == 0)
            printf("TCP failed. Falling back to UDP...\n");
        else
            printf("Trying UDP...\n");
        fd = try_udp(&addr, name);
        if (fd >= 0)
            printf("Connected via UDP.\n");
    }

    if (fd < 0) {
        printf("Unable to connect.\n");
        return 1;
    }

    chat(fd, is_tcp);
    close(fd);
    printf("Goodbye.\n");
    return 0;
}
