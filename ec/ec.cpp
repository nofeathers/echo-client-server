#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif // __linux
#ifdef WIN32
#include <ws2tcpip.h>
#endif // WIN32
#include <iostream>
#include <thread>

#ifdef WIN32
void myerror(const char* msg) { fprintf(stderr, "%s %lu\n", msg, GetLastError()); }
#else
void myerror(const char* msg) { fprintf(stderr, "%s %s %d\n", msg, strerror(errno), errno); }
#endif

static int CID = 0;

void usage() {
    printf("echo-tcp client %s\n",
#include "../version.txt"
    );
    printf("\n");
    printf("syntax: ec <ip> <port> [-si <src ip>] [-sp <src port>]\n");
    printf("sample: ec 127.0.0.1 1234\n");
}

struct Param {
    char* ip{nullptr};
    char* port{nullptr};
    uint32_t srcIp{0};
    uint16_t srcPort{0};

    bool parse(int argc, char* argv[]) {
        for (int i = 1; i < argc;) {
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

            if (strcmp(argv[i], "-sp") == 0) {
                srcPort = atoi(argv[i + 1]);
                i += 2;
                continue;
            }

            ip = argv[i++];
            if (i < argc) port =argv[i++];
        }
        return (ip != nullptr) && (port != nullptr);
    }
} param;

void recvThread(int sd) {
    const int BUFSIZE = 65536;
    char buf[BUFSIZE];

    ssize_t r = ::recv(sd, buf, BUFSIZE - 1, 0);
    if (r <= 0) {
        fprintf(stderr, "Failed to receive CID (recv returned %zd)\n", r);
        myerror("recv");
        std::exit(1);
    }
    buf[r] = '\0';

    if (sscanf(buf, "ID:%d", &CID) != 1) {
        fprintf(stderr, "Invalid ID message: %s\n", buf);
        std::exit(1);
    }
    printf("[Client %d] Connected\n", CID);
    fflush(stdout);

    while (true) {
        ssize_t res = ::recv(sd, buf, BUFSIZE - 1, 0);
        if (res <= 0) {
            fprintf(stderr, "[Client %d] Recv returned %zd\n", CID, res);
            myerror("recv");
            break;
        }
        buf[res] = '\0';
        printf("%s", buf);
        fflush(stdout);
    }
    printf("[Client %d] Disconnected\n", CID);
    fflush(stdout);
    ::close(sd);
    std::exit(0);
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
    // getaddrinfo
    //
    struct addrinfo aiInput, *aiOutput, *ai;
    memset(&aiInput, 0, sizeof(aiInput));
    aiInput.ai_family = AF_INET;
    aiInput.ai_socktype = SOCK_STREAM;
    aiInput.ai_flags = 0;
    aiInput.ai_protocol = 0;

    int res = getaddrinfo(param.ip, param.port, &aiInput, &aiOutput);
    if (res != 0) {
#if defined(WIN32) && defined(UNICODE)
        fprintf(stderr, "getaddrinfo: %S\n", gai_strerror(res));
#else
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(res));
#endif
        return -1;
    }

    //
    // socket
    //
    int sd;
    for (ai = aiOutput; ai != nullptr; ai = ai->ai_next) {
        sd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sd != -1) break;
    }
    if (ai == nullptr) {
        fprintf(stderr, "cann not find socket for %s\n", param.ip);
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
    if (param.srcIp != 0 || param.srcPort != 0) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = param.srcIp;
        addr.sin_port = htons(param.srcPort);

        ssize_t res = ::bind(sd, (struct sockaddr *)&addr, sizeof(addr));
        if (res == -1) {
            myerror("bind");
            return -1;
        }
    }

    //
    // connect
    //
    {
        int res = ::connect(sd, ai->ai_addr, ai->ai_addrlen);
        if (res == -1) {
            myerror("connect");
            return -1;
        }
    }

    std::thread t(recvThread, sd);
    t.detach();

    while (true) {
        std::string s;
        std::getline(std::cin, s);
        s += "\r\n";
        std::string out = "[Client " + std::to_string(CID) + "] " + s;
        printf("%s", out.c_str());
        fflush(stdout);
        ssize_t res = ::send(sd, s.data(), s.size(), 0);
        if (res == 0 || res == -1) {
            fprintf(stderr, "send return %zd", res);
            myerror(" ");
            break;
        }
    }
    ::close(sd);
}
