#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netdb.h>
    #include <unistd.h>
#endif

#include "update_checker.h"
#include "pc/djui/djui.h"
#include "pc/network/version.h"
#include "pc/loading.h"

#define URL_HOST "raw.githubusercontent.com"
#define URL_PATH "/coop-deluxe/sm64coopdx/refs/heads/main/src/pc/network/version.h"
#define VERSION_IDENTIFIER "#define SM64COOPDX_VERSION \""

static char sVersionUpdateTextBuffer[256] = { 0 };
static char sRemoteVersion[8] = { 0 };

void show_update_popup(void) {
    if (sVersionUpdateTextBuffer[0] == '\0') { return; }
    djui_popup_create(sVersionUpdateTextBuffer, 3);
}

void parse_version(const char *data) {
    const char *version = strstr(data, VERSION_IDENTIFIER);
    if (version == NULL) { return; }
    version += strlen(VERSION_IDENTIFIER);
    const char *end = strchr(version, '"');
    if (!end) { return; }
    memcpy(sRemoteVersion, version, end - version);
    sRemoteVersion[end - version] = '\0';
}

int fetch_remote_version(void) {
    char request[512];
    char buffer[1024];
    int sockfd;
    struct addrinfo hints, *res;

#if defined(_WIN32) || defined(_WIN64)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(URL_HOST, "80", &hints, &res) != 0) {
        printf("Failed to resolve host.\n");
        return -1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        printf("Failed to create socket.\n");
        freeaddrinfo(res);
        return -1;
    }

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        printf("Failed to connect to server.\n");
        freeaddrinfo(res);
        close(sockfd);
        return -1;
    }

    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             URL_PATH, URL_HOST);

    send(sockfd, request, strlen(request), 0);

    int total_bytes = 0;
    while (1) {
        int bytes = recv(sockfd, buffer + total_bytes, sizeof(buffer) - total_bytes - 1, 0);
        if (bytes <= 0) break;
        total_bytes += bytes;
    }
    buffer[total_bytes] = '\0';

    close(sockfd);
    freeaddrinfo(res);

#if defined(_WIN32) || defined(_WIN64)
    WSACleanup();
#endif

    if (strstr(buffer, "HTTP/1.1 200 OK") == NULL) {
        printf("Invalid HTTP response.\n");
        return -1;
    }

    // Extract body by finding the first empty line after headers
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4; // skip "\r\n\r\n"
        parse_version(body);
    }

    return 0;
}

void check_for_updates(void) {
    LOADING_SCREEN_MUTEX(loading_screen_set_segment_text("Checking For Updates"));

    if (fetch_remote_version() == 0 && sRemoteVersion[0] != '\0' &&
        strcmp(sRemoteVersion, get_version()) != 0) {
        snprintf(
            sVersionUpdateTextBuffer, sizeof(sVersionUpdateTextBuffer),
            "\\#ffffa0\\%s\n\\#dcdcdc\\%s: %s\n%s: %s",
            DLANG(NOTIF, UPDATE_AVAILABLE),
            DLANG(NOTIF, LATEST_VERSION),
            sRemoteVersion,
            DLANG(NOTIF, YOUR_VERSION),
            get_version()
        );
        gUpdateMessage = true;
    }
}
