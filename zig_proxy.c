/*
 * zig_proxy.c — Windows PE proxy for Wine compilation bridging
 *
 * Replaces the bundled zig.exe inside Wine. When builder.exe invokes zig.exe,
 * this proxy forwards command line arguments and working directory to a local
 * Linux daemon over TCP (127.0.0.1:45678).
 *
 * Compile using native Linux Zig:
 *   zig cc -O2 -target x86_64-windows zig_proxy.c -lws2_32 -o zig.exe
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 45678

static void write_be32(unsigned char *buf, unsigned int val) {
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

static unsigned int read_be32(const unsigned char *buf) {
    return ((unsigned int)buf[0] << 24) |
           ((unsigned int)buf[1] << 16) |
           ((unsigned int)buf[2] << 8)  |
           ((unsigned int)buf[3]);
}

static int recv_all(SOCKET s, void *buf, int len) {
    int total = 0;
    char *p = (char *)buf;
    while (total < len) {
        int n = recv(s, p + total, len - total, 0);
        if (n <= 0) return total;
        total += n;
    }
    return total;
}

int main(int argc, char *argv[]) {
    // Fast path: version check
    if (argc >= 2 && (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0)) {
        printf("0.15.2\n");
        return 0;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    SOCKET s = INVALID_SOCKET;
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serverAddr.sin_port = htons(PORT);

    // Robust connect retry loop (up to 150 attempts with 100ms backoff = 15s)
    // Prevents connection drops under heavy multi-core parallel builds
    int connected = 0;
    for (int retry = 0; retry < 150; retry++) {
        if (s != INVALID_SOCKET) {
            closesocket(s);
        }
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            Sleep(50);
            continue;
        }

        if (connect(s, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) != SOCKET_ERROR) {
            connected = 1;
            break;
        }

        Sleep(100);
    }

    if (!connected) {
        fprintf(stderr, "Error: zig_daemon is not running on 127.0.0.1:%d (connection timed out)\n", PORT);
        if (s != INVALID_SOCKET) closesocket(s);
        WSACleanup();
        return 1;
    }

    // Get current working directory
    char cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, cwd);

    // Build JSON payload: {"cwd": "...", "args": ["...", "..."]}
    size_t est_size = strlen(cwd) * 2 + 128;
    for (int i = 1; i < argc; i++) {
        est_size += strlen(argv[i]) * 2 + 16;
    }

    char *json = (char *)malloc(est_size);
    if (!json) {
        closesocket(s);
        WSACleanup();
        return 1;
    }

    char *p = json;
    p += sprintf(p, "{\"cwd\":\"");
    for (char *c = cwd; *c; c++) {
        if (*c == '\\') { *p++ = '\\'; *p++ = '\\'; }
        else if (*c == '"') { *p++ = '\\'; *p++ = '"'; }
        else { *p++ = *c; }
    }
    p += sprintf(p, "\",\"args\":[");

    for (int i = 1; i < argc; i++) {
        if (i > 1) *p++ = ',';
        *p++ = '"';
        for (char *c = argv[i]; *c; c++) {
            if (*c == '\\') { *p++ = '\\'; *p++ = '\\'; }
            else if (*c == '"') { *p++ = '\\'; *p++ = '"'; }
            else if (*c == '\n') { *p++ = '\\'; *p++ = 'n'; }
            else if (*c == '\r') { *p++ = '\\'; *p++ = 'r'; }
            else if (*c == '\t') { *p++ = '\\'; *p++ = 't'; }
            else { *p++ = *c; }
        }
        *p++ = '"';
    }
    p += sprintf(p, "]}");
    unsigned int json_len = (unsigned int)(p - json);

    // Send length prefix (4 bytes) + JSON payload
    unsigned char len_buf[4];
    write_be32(len_buf, json_len);
    send(s, (const char *)len_buf, 4, 0);
    send(s, json, json_len, 0);
    free(json);

    // Read responses from Linux daemon
    int final_exit_code = 0;
    while (1) {
        unsigned char frame_type;
        int n = recv_all(s, &frame_type, 1);
        if (n <= 0) break;

        if (frame_type == 1) { // stdout
            unsigned char chunk_len_buf[4];
            if (recv_all(s, chunk_len_buf, 4) < 4) break;
            unsigned int chunk_len = read_be32(chunk_len_buf);
            char *buf = (char *)malloc(chunk_len);
            if (buf) {
                recv_all(s, buf, chunk_len);
                fwrite(buf, 1, chunk_len, stdout);
                fflush(stdout);
                free(buf);
            }
        } else if (frame_type == 2) { // stderr
            unsigned char chunk_len_buf[4];
            if (recv_all(s, chunk_len_buf, 4) < 4) break;
            unsigned int chunk_len = read_be32(chunk_len_buf);
            char *buf = (char *)malloc(chunk_len);
            if (buf) {
                recv_all(s, buf, chunk_len);
                fwrite(buf, 1, chunk_len, stderr);
                fflush(stderr);
                free(buf);
            }
        } else if (frame_type == 3) { // exit code
            unsigned char code_buf[4];
            if (recv_all(s, code_buf, 4) == 4) {
                final_exit_code = (int)read_be32(code_buf);
            }
            break;
        } else {
            break;
        }
    }

    closesocket(s);
    WSACleanup();
    return final_exit_code;
}
