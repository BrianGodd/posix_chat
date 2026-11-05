# POSIX Chat Client / Server (C)

A small POSIX-compatible IPv4 chat room, using the standard socket API and `poll()`.

Features:
- TCP and UDP on the same server port.
- **Automatic connection fallback**: client in `auto` mode tries TCP first, then UDP if TCP connection or welcome handshake fails.
- Broadcast chat, `/list`, and private messages with `/msg`.
- **Cross-protocol private messages**: a TCP user can message a UDP user and vice versa.
- UDP application-level `HELLO` / `WELCOME UDP` verification, retries, and periodic keepalives.
- Basic username validation and uniqueness (1–31 English letters, numbers, `_`, `-`).

**Scope:** Automatic fallback happens **at initial connection**. It does not automatically migrate an already connected TCP session when the connection drops. UDP messages are not guaranteed to arrive and there is no encryption or authentication. This is a teaching demo, not a secure production messenger.

## Requirements

Linux, macOS, or another suitable POSIX environment with a C compiler and `make`.
Windows users can use WSL (Ubuntu). Native Windows Winsock is not supported.

## Build

```bash
make
```

Or compile without Makefile:

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -o chat_server chat_server.c
cc -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -o chat_client chat_client.c
```

## Run a chat room (three separate terminals)

Terminal 1:

```bash
./chat_server 5555 both
```

Terminal 2:

```bash
./chat_client 127.0.0.1 Wayne 5555 auto
```

Terminal 3:

```bash
./chat_client 127.0.0.1 Alice 5555 udp
```

The short form below is also valid. The defaults are port **5555** and client mode **auto**:

```bash
./chat_client 127.0.0.1 Wayne
```

Client commands:

```text
Hello everyone!
/list
/msg Alice Hi Alice, only you can see this.
/quit
/help
```

## Demonstrate BONUS 1: Automatic fallback

Stop the existing server, then start a server that listens **only on UDP**:

```bash
./chat_server 5555 udp
```

From another terminal, connect with `auto`:

```bash
./chat_client 127.0.0.1 Wayne 5555 auto
```

Expected output:

```text
Trying TCP...
TCP failed. Falling back to UDP...
Connected via UDP.
```

The client proves UDP works by sending `HELLO Wayne` and waiting for `WELCOME UDP`; UDP `connect()` alone does not establish a connection.

## Demonstrate BONUS 2: Private messages across protocols

Stop the old server, then start a server for both protocols:

```bash
./chat_server 5555 both
```

Terminal 2 (TCP):

```bash
./chat_client 127.0.0.1 Wayne 5555 tcp
```

Terminal 3 (UDP):

```bash
./chat_client 127.0.0.1 Alice 5555 udp
```

Wayne types:

```text
/msg Alice Hello from TCP!
```

Only Alice sees:

```text
[PM from Wayne] Hello from TCP!
```

Alice can reply with `/msg Wayne Hello from UDP!`.

## Server and client usage

```text
./chat_server [port] [tcp|udp|both]
./chat_client <server-host-or-IPv4> <username> [port] [tcp|udp|auto]
```

- Server default port = 5555, mode = both.
- Client default port = 5555, mode = auto.
- Servers bind to 0.0.0.0 (all IPv4 interfaces).
- To use two computers, run the server on one computer and pass that machine's reachable IPv4 address to the clients; allow your chosen TCP and UDP port through any firewall.
- Use `/quit` or Ctrl+C to exit. UDP inactive sessions expire after 90 seconds; clients send keepalive PING every 20 seconds.

## Protocol summary

Client to server (one line each): `HELLO name`, `MSG text`, `PM target text`, `LIST`, `PING`, `QUIT`.

Server to client (one line each): `WELCOME TCP/UDP`, `[name] message`, `[PM from name] text`, `[PM to target] text`, `[users] ...`, `ERROR ...`.

Each line is separated by `\n`. TCP is a byte stream, so the server/client assemble lines; UDP receives one line per datagram.

## POSIX notes

Uses `socket`, `bind`, `listen`, `accept`, `connect`, `recv`, `send`, `recvfrom`, `sendto`, `close`, `getaddrinfo`, `fcntl`, `poll`, `signal`, and `time`. No Windows-specific Winsock and no Linux-only `epoll` calls. Intended for a POSIX environment (Linux/macOS).
