#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Url {
    bool isHttp = false;
    std::string scheme;
    std::string host;
    std::string port = "80";
    std::string path = "/";
};

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t pos = 0;
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) ++pos;
    return s.substr(pos);
}

bool parseUrl(const std::string& input, Url& url) {
    if (!startsWith(input, "http://")) return false;
    url.isHttp = true;
    url.scheme = "http";
    std::string rest = input.substr(7);
    size_t slash = rest.find('/');
    std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    url.path = slash == std::string::npos ? "/" : rest.substr(slash);
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        url.host = authority.substr(0, colon);
        url.port = authority.substr(colon + 1);
    } else {
        url.host = authority;
    }
    return !url.host.empty();
}

std::string baseUrl(const std::string& url) {
    size_t slash = url.find_last_of('/');
    return slash == std::string::npos ? "" : url.substr(0, slash + 1);
}

std::string joinUrl(const std::string& base, const std::string& uri) {
    if (startsWith(uri, "http://") || startsWith(uri, "https://")) return uri;
    if (startsWith(uri, "/")) {
        Url parsed;
        if (parseUrl(base, parsed)) {
            return parsed.scheme + "://" + parsed.host + (parsed.port == "80" ? "" : ":" + parsed.port) + uri;
        }
    }
    return base + uri;
}

bool sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

std::string httpGet(const Url& url) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    if (getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &results) != 0) return {};

    int fd = -1;
    for (addrinfo* p = results; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd >= 0 && connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        if (fd >= 0) close(fd);
        fd = -1;
    }
    freeaddrinfo(results);
    if (fd < 0) return {};

    std::ostringstream req;
    req << "GET " << url.path << " HTTP/1.1\r\n"
        << "Host: " << url.host << "\r\n"
        << "Connection: close\r\n\r\n";
    if (!sendAll(fd, req.str())) {
        close(fd);
        return {};
    }

    std::string response;
    char buf[4096];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        response.append(buf, static_cast<size_t>(n));
    }
    close(fd);

    size_t body = response.find("\r\n\r\n");
    return body == std::string::npos ? response : response.substr(body + 4);
}

std::string readInput(const std::string& input) {
    Url url;
    if (parseUrl(input, url)) return httpGet(url);
    std::ifstream file(input);
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void parsePlaylist(const std::string& text, const std::string& source) {
    std::istringstream in(text);
    std::string line;
    std::string currentInf;
    std::string base = startsWith(source, "http://") ? baseUrl(source) : "";
    bool isMaster = false;
    int mediaCount = 0;

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (startsWith(line, "#EXT-X-TARGETDURATION:")) {
            std::cout << "targetDuration=" << line.substr(22) << std::endl;
        } else if (startsWith(line, "#EXT-X-STREAM-INF:")) {
            isMaster = true;
            std::cout << "variant=" << line.substr(18) << std::endl;
        } else if (startsWith(line, "#EXTINF:")) {
            currentInf = line.substr(8);
        } else if (line[0] != '#') {
            std::string full = base.empty() ? line : joinUrl(base, line);
            if (isMaster && currentInf.empty()) {
                std::cout << "subPlaylist=" << full << std::endl;
            } else {
                std::cout << "segment[" << mediaCount++ << "] duration=" << currentInf
                          << " url=" << full << std::endl;
                currentInf.clear();
            }
        }
    }
    std::cout << "playlistType=" << (isMaster ? "master" : "media")
              << " segments=" << mediaCount << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <local.m3u8|http-url>\n";
        return 1;
    }
    std::string text = readInput(argv[1]);
    if (text.empty()) {
        std::cerr << "Failed to read input.\n";
        return 1;
    }
    parsePlaylist(text, argv[1]);
    return 0;
}
