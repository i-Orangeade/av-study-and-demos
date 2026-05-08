#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <string>

namespace {

struct RtspUrl { std::string host, port = "554", path = "/"; };

bool parseUrl(const std::string& input, RtspUrl& url) {
    if (input.rfind("rtsp://", 0) != 0) return false;
    std::string rest = input.substr(7);
    size_t slash = rest.find('/');
    std::string auth = slash == std::string::npos ? rest : rest.substr(0, slash);
    url.path = slash == std::string::npos ? "/" : "/" + rest.substr(slash + 1);
    size_t colon = auth.rfind(':');
    url.host = colon == std::string::npos ? auth : auth.substr(0, colon);
    if (colon != std::string::npos) url.port = auth.substr(colon + 1);
    return !url.host.empty();
}

int connectTcp(const RtspUrl& url) {
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

std::string recvResponse(int fd) {
    std::string data;
    char buf[2048];
    while (data.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        data.append(buf, static_cast<size_t>(n));
    }
    size_t contentLength = 0;
    size_t pos = data.find("Content-Length:");
    if (pos != std::string::npos) {
        contentLength = static_cast<size_t>(std::atoi(data.c_str() + pos + 15));
    }
    size_t bodyPos = data.find("\r\n\r\n");
    while (bodyPos != std::string::npos && data.size() < bodyPos + 4 + contentLength) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        data.append(buf, static_cast<size_t>(n));
    }
    return data;
}

std::string request(int fd, const std::string& method, const std::string& url, int& cseq, const std::string& extra = "") {
    std::ostringstream req;
    req << method << " " << url << " RTSP/1.0\r\n"
        << "CSeq: " << cseq++ << "\r\n"
        << "User-Agent: av-study-rtsp-client/1.0\r\n";
    if (!extra.empty()) req << extra;
    req << "\r\n";
    std::string text = req.str();
    send(fd, text.data(), text.size(), 0);
    std::string resp = recvResponse(fd);
    std::cout << "\n--- " << method << " response ---\n" << resp << std::endl;
    return resp;
}

std::string findControl(const std::string& sdp) {
    std::istringstream in(sdp);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("a=control:", 0) == 0 && line.find("track") != std::string::npos) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            return line.substr(10);
        }
    }
    return "";
}

std::string findSession(const std::string& response) {
    size_t pos = response.find("Session:");
    if (pos == std::string::npos) return "";
    size_t begin = pos + 8;
    while (begin < response.size() && response[begin] == ' ') ++begin;
    size_t end = response.find_first_of(";\r\n", begin);
    return response.substr(begin, end - begin);
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " rtsp://host[:port]/path\n";
        return 1;
    }
    RtspUrl parsed;
    if (!parseUrl(argv[1], parsed)) {
        std::cerr << "Invalid RTSP URL.\n";
        return 1;
    }
    int fd = connectTcp(parsed);
    if (fd < 0) {
        std::cerr << "TCP connect failed.\n";
        return 1;
    }
    int cseq = 1;
    std::string url = argv[1];
    request(fd, "OPTIONS", url, cseq);
    std::string describe = request(fd, "DESCRIBE", url, cseq, "Accept: application/sdp\r\n");
    std::string control = findControl(describe);
    std::cout << "first media control=" << control << std::endl;

    std::string setupUrl = control.empty() ? url : (control.rfind("rtsp://", 0) == 0 ? control : url + "/" + control);
    std::string setup = request(fd, "SETUP", setupUrl, cseq, "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n");
    std::string session = findSession(setup);
    if (!session.empty()) {
        request(fd, "PLAY", url, cseq, "Session: " + session + "\r\n");
        request(fd, "TEARDOWN", url, cseq, "Session: " + session + "\r\n");
    }
    close(fd);
    return 0;
}
