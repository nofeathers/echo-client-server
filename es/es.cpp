#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <mutex>
#include <algorithm>
#ifdef __linux__
#include <arpa/inet.h>
#include <sys/socket.h>
#endif // __linux
#ifdef WIN32
#include <ws2tcpip.h>
#endif // WIN32
#include <thread>

#ifdef WIN32
void myerror(const char* msg) { fprintf(stderr, "%s %lu\n", msg, GetLastError()); }
#else
void myerror(const char* msg) { fprintf(stderr, "%s %s %d\n", msg, strerror(errno), errno); }
#endif

static std::vector<int> clients;
static std::mutex clients_mutex;
static std::atomic <int> counter{0};

void usage() {
    printf("echo-tcp server %s\n",
#include "../version.txt"
   );
    printf("\n");
    printf("syntax: es <port> [-e] [-b] [-si <src ip>]\n");
    printf("  -e : echo\n");
    printf("  -b : broadcast\n");
    printf("sample: ts 1234\n");
}

struct Param {
    bool echo{false};
    bool broadcast{false};
    uint16_t port{0};
    uint32_t srcIp{0};

    bool parse(int argc, char* argv[]) {
        for (int i = 1; i < argc;) {
            if (strcmp(argv[i], "-b") == 0) {
                broadcast = true;
                i++;
                continue;
            }
            if (strcmp(argv[i], "-e") == 0) {
                echo = true;
                i++;
                continue;
            }

            if (strcmp(argv[i], "-si") == 0) {
                int res = inet_pton(AF_INET, argv[i + 1], &srcIp);
                switch (res) {
                    case 1: break;
                    case 0: fprintf(stderr, "not a valid network address\n"); return false;
                    case -1: myerror("inet_pton"); return false;
                }
                i += 2;
                continue;
            }

            if (i < argc) port = atoi(argv[i++]);
        }
        return port != 0;
    }
} param;

void recvThread(int sd, int CID) {

    printf("[Client %d]Connected\n", CID);
    fflush(stdout);
    static const int BUFSIZE = 65536;
    char buf[BUFSIZE];
    while (true) {
        ssize_t res = ::recv(sd, buf, BUFSIZE - 1, 0);
        if (res == 0 || res == -1) {
            fprintf(stderr, "[Client %d]Recv return %zd", CID, res);
            myerror(" ");
            break;
        }
        if (param.broadcast) {
            std::lock_guard<std::mutex> lg(clients_mutex);
            char sendBuf[BUFSIZE + 32];
            int prefixLen = snprintf(sendBuf, sizeof(sendBuf), "[Client %d] %.*s", CID, (int)res, buf);
            for(int client_sd : clients) {
                if (client_sd == sd) continue;
                send(client_sd, sendBuf, prefixLen, 0);
            }
        }

        buf[res] = '\0';
        printf("[Client %d] %s", CID, buf);
        fflush(stdout);
        if (param.echo) {
            res = ::send(sd, buf, res, 0);
            if (res == 0 || res == -1) {
                fprintf(stderr, "[Client %d]Send return %zd", CID, res);
                myerror(" ");
                break;
            }
        }
    }

    if (param.broadcast) {
        std::lock_guard<std::mutex> lg(clients_mutex);
        clients.erase(
            std::remove(clients.begin(), clients.end(), sd),
            clients.end()
            );
    }

    printf("[Client %d]Disconnected\n", CID);
    fflush(stdout);
    ::close(sd);
}

int main(int argc, char* argv[]) {
    if (!param.parse(argc, argv)) {
        usage();
        return -1;
    }

#ifdef WIN32
    WSAData wsaData;
    WSAStartup(0x0202, &wsaData);
#endif // WIN32

    //
    // socket
    //
    int sd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        myerror("socket");
        return -1;
    }

#ifdef __linux__
    //
    // setsockopt
    //
    {
        int optval = 1;
        int res = ::setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        if (res == -1) {
            myerror("setsockopt");
            return -1;
        }
    }
#endif // __linux

    //
    // bind
    //
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = param.srcIp;
        addr.sin_port = htons(param.port);

        ssize_t res = ::bind(sd, (struct sockaddr *)&addr, sizeof(addr));
        if (res == -1) {
            myerror("bind");
            return -1;
        }
    }

    //
    // listen
    //
    {
        int res = listen(sd, 5);
        if (res == -1) {
            myerror("listen");
            return -1;
        }
    }

    while (true) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int newsd = ::accept(sd, (struct sockaddr *)&addr, &len);
        if (newsd == -1) {
            myerror("accept");
            break;
        }

        int CID = ++counter;
        char idmsg[32];
        int n = snprintf(idmsg, sizeof(idmsg), "ID:%d\r\n", CID);
        ::send(newsd, idmsg, n, 0);

        if (param.broadcast) {
            std::lock_guard<std::mutex> lg(clients_mutex);
            clients.push_back(newsd);
        }
        std::thread* t = new std::thread(recvThread, newsd, CID);
        t->detach();
    }
    ::close(sd);
}
