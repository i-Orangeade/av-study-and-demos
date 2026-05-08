#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

void usage(const char* program) {
    std::cerr
        << "Usage:\n"
        << "  " << program << " send <broadcast-ip> <port> <interval-ms> <message>\n"
        << "  " << program << " recv <port>\n\n"
        << "Examples:\n"
        << "  " << program << " send 255.255.255.255 9001 1000 hello\n"
        << "  " << program << " recv 9001\n";
}

bool parsePort(const char* text, uint16_t& port) {
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || value <= 0 || value > 65535) {
        return false;
    }
    port = static_cast<uint16_t>(value);
    return true;
}

int runSender(const std::string& ip, uint16_t port, int intervalMs, const std::string& message) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) < 0) {
        perror("setsockopt(SO_BROADCAST)");
        close(fd);
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "Invalid IPv4 address: " << ip << std::endl;
        close(fd);
        return 1;
    }

    std::cout << "[send] broadcast to " << ip << ":" << port
              << ", interval=" << intervalMs << "ms" << std::endl;

    uint64_t seq = 0;
    while (true) {
        std::string payload = "seq=" + std::to_string(seq++) + " " + message;
        ssize_t n = sendto(fd,
                           payload.data(),
                           payload.size(),
                           0,
                           reinterpret_cast<sockaddr*>(&addr),
                           sizeof(addr));
        if (n < 0) {
            perror("sendto");
            close(fd);
            return 1;
        }

        std::cout << "[send] " << payload << " (" << n << " bytes)" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }
}

int runReceiver(uint16_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return 1;
    }

    std::cout << "[recv] listening on UDP port " << port << std::endl;

    std::vector<char> buffer(2048);
    while (true) {
        sockaddr_in peer{};
        socklen_t peerLen = sizeof(peer);
        ssize_t n = recvfrom(fd,
                             buffer.data(),
                             buffer.size() - 1,
                             0,
                             reinterpret_cast<sockaddr*>(&peer),
                             &peerLen);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom");
            close(fd);
            return 1;
        }

        buffer[static_cast<size_t>(n)] = '\0';
        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        std::cout << "[recv] from " << ip << ":" << ntohs(peer.sin_port)
                  << " -> " << buffer.data() << std::endl;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];
    if (mode == "send") {
        if (argc < 6) {
            usage(argv[0]);
            return 1;
        }
        uint16_t port = 0;
        if (!parsePort(argv[3], port)) {
            std::cerr << "Invalid port: " << argv[3] << std::endl;
            return 1;
        }
        int intervalMs = std::max(1, std::atoi(argv[4]));
        return runSender(argv[2], port, intervalMs, argv[5]);
    }

    if (mode == "recv") {
        if (argc != 3) {
            usage(argv[0]);
            return 1;
        }
        uint16_t port = 0;
        if (!parsePort(argv[2], port)) {
            std::cerr << "Invalid port: " << argv[2] << std::endl;
            return 1;
        }
        return runReceiver(port);
    }

    usage(argv[0]);
    return 1;
}
