#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Url { std::string host, port = "80", path = "/"; };
struct Segment { double duration = 0.0; std::string url; };

bool startsWith(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }
std::string trim(std::string s) { while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back(); return s; }

bool parseUrl(const std::string& input, Url& u) {
    if (!startsWith(input, "http://")) return false;
    std::string rest = input.substr(7);
    size_t slash = rest.find('/');
    std::string auth = slash == std::string::npos ? rest : rest.substr(0, slash);
    u.path = slash == std::string::npos ? "/" : rest.substr(slash);
    size_t colon = auth.rfind(':');
    u.host = colon == std::string::npos ? auth : auth.substr(0, colon);
    if (colon != std::string::npos) u.port = auth.substr(colon + 1);
    return !u.host.empty();
}

std::string baseUrl(const std::string& url) {
    size_t slash = url.find_last_of('/');
    return slash == std::string::npos ? "" : url.substr(0, slash + 1);
}

std::string joinUrl(const std::string& base, const std::string& uri) {
    if (startsWith(uri, "http://") || startsWith(uri, "https://")) return uri;
    if (startsWith(uri, "/")) {
        Url u; if (parseUrl(base, u)) return "http://" + u.host + (u.port == "80" ? "" : ":" + u.port) + uri;
    }
    return base + uri;
}

std::string httpGet(const std::string& input) {
    Url u; if (!parseUrl(input, u)) return {};
    addrinfo hints{}; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(u.host.c_str(), u.port.c_str(), &hints, &res) != 0) return {};
    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd >= 0 && connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        if (fd >= 0) close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return {};
    std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host + "\r\nConnection: close\r\n\r\n";
    send(fd, req.data(), req.size(), 0);
    std::string data; char buf[4096];
    while (true) { ssize_t n = recv(fd, buf, sizeof(buf), 0); if (n <= 0) break; data.append(buf, n); }
    close(fd);
    size_t pos = data.find("\r\n\r\n");
    return pos == std::string::npos ? data : data.substr(pos + 4);
}

std::vector<Segment> parseSegments(const std::string& playlist, const std::string& sourceUrl) {
    std::vector<Segment> segments;
    std::istringstream in(playlist);
    std::string line;
    double duration = 0.0;
    std::string base = baseUrl(sourceUrl);
    while (std::getline(in, line)) {
        line = trim(line);
        if (startsWith(line, "#EXTINF:")) {
            duration = std::atof(line.substr(8).c_str());
        } else if (!line.empty() && line[0] != '#') {
            segments.push_back({duration, joinUrl(base, line)});
            duration = 0.0;
        }
    }
    return segments;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " http://host/path/index.m3u8\n";
        return 1;
    }
    std::string playlist = httpGet(argv[1]);
    if (playlist.empty()) {
        std::cerr << "Failed to download playlist.\n";
        return 1;
    }
    auto segments = parseSegments(playlist, argv[1]);
    std::cout << "segments=" << segments.size() << std::endl;
    for (size_t i = 0; i < segments.size() && i < 5; ++i) {
        std::cout << "preload candidate[" << i << "] duration=" << segments[i].duration
                  << " url=" << segments[i].url << std::endl;
    }
    std::cout << "Learning version stops at playlist/segment scheduling. TS demux + FFmpeg/SDL2 playback can be added next." << std::endl;
    return 0;
}
