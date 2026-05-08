#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Url { std::string host, port = "80", path = "/"; };

uint32_t u24(const uint8_t* p) { return (p[0] << 16) | (p[1] << 8) | p[2]; }

bool parseUrl(const std::string& input, Url& url) {
    if (input.rfind("http://", 0) != 0) return false;
    std::string rest = input.substr(7);
    size_t slash = rest.find('/');
    std::string auth = slash == std::string::npos ? rest : rest.substr(0, slash);
    url.path = slash == std::string::npos ? "/" : rest.substr(slash);
    size_t colon = auth.rfind(':');
    url.host = colon == std::string::npos ? auth : auth.substr(0, colon);
    if (colon != std::string::npos) url.port = auth.substr(colon + 1);
    return !url.host.empty();
}

int connectHttp(const Url& url) {
    addrinfo hints{}; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &res) != 0) return -1;
    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd >= 0 && connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        if (fd >= 0) close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

bool sendRequest(int fd, const Url& url) {
    std::ostringstream req;
    req << "GET " << url.path << " HTTP/1.1\r\n"
        << "Host: " << url.host << "\r\n"
        << "Accept: */*\r\n"
        << "Connection: close\r\n\r\n";
    std::string text = req.str();
    return send(fd, text.data(), text.size(), 0) == static_cast<ssize_t>(text.size());
}

bool readUntilHeaderEnd(int fd, std::vector<uint8_t>& bodyPrefix) {
    std::string data;
    char buf[1024];
    while (data.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        data.append(buf, static_cast<size_t>(n));
    }
    size_t pos = data.find("\r\n\r\n");
    std::cout << data.substr(0, pos) << std::endl;
    bodyPrefix.assign(data.begin() + static_cast<long>(pos + 4), data.end());
    return true;
}

bool take(std::vector<uint8_t>& buffer, int fd, size_t n, std::vector<uint8_t>& out) {
    while (buffer.size() < n) {
        uint8_t temp[4096];
        ssize_t r = recv(fd, temp, sizeof(temp), 0);
        if (r <= 0) return false;
        buffer.insert(buffer.end(), temp, temp + r);
    }
    out.assign(buffer.begin(), buffer.begin() + static_cast<long>(n));
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<long>(n));
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " http://host/live.flv\n";
        return 1;
    }
    Url url;
    if (!parseUrl(argv[1], url)) {
        std::cerr << "Only http:// URL is supported in this learning demo.\n";
        return 1;
    }
    int fd = connectHttp(url);
    if (fd < 0 || !sendRequest(fd, url)) {
        std::cerr << "HTTP connect/request failed.\n";
        return 1;
    }

    std::vector<uint8_t> buffer;
    if (!readUntilHeaderEnd(fd, buffer)) {
        std::cerr << "Failed to read HTTP headers.\n";
        return 1;
    }

    std::vector<uint8_t> flvHeader;
    if (!take(buffer, fd, 13, flvHeader) || flvHeader[0] != 'F' || flvHeader[1] != 'L' || flvHeader[2] != 'V') {
        std::cerr << "Response body is not FLV.\n";
        return 1;
    }
    std::cout << "FLV stream detected. Printing first tags..." << std::endl;

    for (int i = 0; i < 30; ++i) {
        std::vector<uint8_t> h;
        if (!take(buffer, fd, 11, h)) break;
        uint8_t type = h[0];
        uint32_t size = u24(h.data() + 1);
        uint32_t ts = u24(h.data() + 4) | (h[7] << 24);
        std::vector<uint8_t> payload;
        if (!take(buffer, fd, size + 4, payload)) break; // payload + PreviousTagSize
        std::cout << "tag=" << i << " type=" << static_cast<int>(type)
                  << " timestamp=" << ts << " size=" << size << std::endl;
    }

    close(fd);
    std::cout << "Learning version stops after tag parsing. FFmpeg/SDL2 decode can be added here." << std::endl;
    return 0;
}
