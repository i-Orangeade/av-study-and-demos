#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

const int DEFAULT_PORT = 9000;
const int MAX_EVENTS = 64;
const int BUFFER_SIZE = 4096;

struct ClientState {
    std::string pendingOutput;
};

void printUsage(const char* program) {
    std::cerr
        << "Usage:\n"
        << "  " << program << " server [port]\n"
        << "  " << program << " client [host] [port]\n\n"
        << "Build:\n"
        << "  cmake -S Demos/AV-Net-TCP-Echo -B Demos/AV-Net-TCP-Echo/build\n"
        << "  cmake --build Demos/AV-Net-TCP-Echo/build\n";
}

int parsePort(const char* text) {
    char* end = nullptr;
    long port = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || port <= 0 || port > 65535) {
        std::cerr << "Invalid port: " << text << std::endl;
        return -1;
    }
    return static_cast<int>(port);
}

bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL)");
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL)");
        return false;
    }

    return true;
}

bool addEpollFd(int epollFd, int fd, uint32_t events) {
    epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl(ADD)");
        return false;
    }

    return true;
}

bool modifyEpollFd(int epollFd, int fd, uint32_t events) {
    epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        perror("epoll_ctl(MOD)");
        return false;
    }

    return true;
}

void closeClient(int epollFd,
                 std::unordered_map<int, ClientState>& clients,
                 int clientFd) {
    epoll_ctl(epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
    close(clientFd);
    clients.erase(clientFd);
    std::cout << "[server] client disconnected, fd=" << clientFd << std::endl;
}

int createListenSocket(int port) {
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd == -1) {
        perror("socket");
        return -1;
    }

    int reuse = 1;
    if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        perror("setsockopt(SO_REUSEADDR)");
        close(listenFd);
        return -1;
    }

    if (!setNonBlocking(listenFd)) {
        close(listenFd);
        return -1;
    }

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        perror("bind");
        close(listenFd);
        return -1;
    }

    if (listen(listenFd, SOMAXCONN) == -1) {
        perror("listen");
        close(listenFd);
        return -1;
    }

    return listenFd;
}

void acceptClients(int epollFd,
                   int listenFd,
                   std::unordered_map<int, ClientState>& clients) {
    while (true) {
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(listenFd,
                              reinterpret_cast<sockaddr*>(&clientAddr),
                              &clientLen);

        if (clientFd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            perror("accept");
            return;
        }

        if (!setNonBlocking(clientFd)) {
            close(clientFd);
            continue;
        }

        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
        std::cout << "[server] client connected, fd=" << clientFd
                  << ", addr=" << ip << ":" << ntohs(clientAddr.sin_port)
                  << std::endl;

        clients[clientFd] = ClientState();
        if (!addEpollFd(epollFd, clientFd, EPOLLIN | EPOLLRDHUP)) {
            clients.erase(clientFd);
            close(clientFd);
        }
    }
}

bool readClientData(int clientFd, ClientState& client) {
    char buffer[BUFFER_SIZE];

    while (true) {
        ssize_t n = recv(clientFd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            client.pendingOutput.append(buffer, static_cast<size_t>(n));
            continue;
        }

        if (n == 0) {
            return false;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }

        perror("recv");
        return false;
    }
}

bool writeClientData(int clientFd, ClientState& client) {
    while (!client.pendingOutput.empty()) {
        ssize_t n = send(clientFd,
                         client.pendingOutput.data(),
                         client.pendingOutput.size(),
                         0);

        if (n > 0) {
            client.pendingOutput.erase(0, static_cast<size_t>(n));
            continue;
        }

        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }

        perror("send");
        return false;
    }

    return true;
}

int runServer(int port) {
    signal(SIGPIPE, SIG_IGN);

    int listenFd = createListenSocket(port);
    if (listenFd == -1) {
        return 1;
    }

    int epollFd = epoll_create1(0);
    if (epollFd == -1) {
        perror("epoll_create1");
        close(listenFd);
        return 1;
    }

    if (!addEpollFd(epollFd, listenFd, EPOLLIN)) {
        close(epollFd);
        close(listenFd);
        return 1;
    }

    std::unordered_map<int, ClientState> clients;
    std::vector<epoll_event> events(MAX_EVENTS);

    std::cout << "[server] listening on 0.0.0.0:" << port << std::endl;

    while (true) {
        int ready = epoll_wait(epollFd, events.data(), events.size(), -1);
        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < ready; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == listenFd) {
                acceptClients(epollFd, listenFd, clients);
                continue;
            }

            auto it = clients.find(fd);
            if (it == clients.end()) {
                continue;
            }

            bool alive = true;
            if (ev & EPOLLIN) {
                alive = readClientData(fd, it->second);
            }

            if (alive && (ev & EPOLLOUT)) {
                alive = writeClientData(fd, it->second);
            }

            if (!alive || (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))) {
                closeClient(epollFd, clients, fd);
                continue;
            }

            uint32_t nextEvents = EPOLLIN | EPOLLRDHUP;
            if (!it->second.pendingOutput.empty()) {
                nextEvents |= EPOLLOUT;
            }

            if (!modifyEpollFd(epollFd, fd, nextEvents)) {
                closeClient(epollFd, clients, fd);
            }
        }
    }

    for (auto& item : clients) {
        close(item.first);
    }
    close(epollFd);
    close(listenFd);
    return 1;
}

bool sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }

        if (n == -1 && errno == EINTR) {
            continue;
        }

        perror("send");
        return false;
    }

    return true;
}

bool recvExact(int fd, size_t expected, std::string& output) {
    output.clear();
    output.reserve(expected);

    char buffer[BUFFER_SIZE];
    while (output.size() < expected) {
        size_t need = expected - output.size();
        size_t chunkSize = need < sizeof(buffer) ? need : sizeof(buffer);
        ssize_t n = recv(fd, buffer, chunkSize, 0);

        if (n > 0) {
            output.append(buffer, static_cast<size_t>(n));
            continue;
        }

        if (n == 0) {
            std::cerr << "[client] server closed connection" << std::endl;
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        perror("recv");
        return false;
    }

    return true;
}

int runClient(const std::string& host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return 1;
    }

    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) != 1) {
        std::cerr << "Invalid IPv4 address: " << host << std::endl;
        close(fd);
        return 1;
    }

    if (connect(fd, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == -1) {
        perror("connect");
        close(fd);
        return 1;
    }

    std::cout << "[client] connected to " << host << ":" << port << std::endl;
    std::cout << "[client] input text and press Enter, Ctrl+D to quit" << std::endl;

    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
        std::string message = line + '\n';
        if (!sendAll(fd, message)) {
            close(fd);
            return 1;
        }

        std::string echoed;
        if (!recvExact(fd, message.size(), echoed)) {
            close(fd);
            return 1;
        }

        std::cout << "[echo] " << echoed;
    }

    close(fd);
    std::cout << "\n[client] bye" << std::endl;
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];
    if (mode == "server") {
        int port = DEFAULT_PORT;
        if (argc >= 3) {
            port = parsePort(argv[2]);
            if (port == -1) {
                return 1;
            }
        }
        return runServer(port);
    }

    if (mode == "client") {
        std::string host = "127.0.0.1";
        int port = DEFAULT_PORT;

        if (argc >= 3) {
            host = argv[2];
        }
        if (argc >= 4) {
            port = parsePort(argv[3]);
            if (port == -1) {
                return 1;
            }
        }

        return runClient(host, port);
    }

    printUsage(argv[0]);
    return 1;
}
