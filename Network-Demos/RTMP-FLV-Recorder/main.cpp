#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

struct RtmpUrl { std::string host, port = "1935", app, stream, tcUrl; };

bool parseUrl(const std::string& input, RtmpUrl& u) {
    if (input.rfind("rtmp://", 0) != 0) return false;
    std::string rest = input.substr(7);
    size_t slash = rest.find('/');
    std::string auth = slash == std::string::npos ? rest : rest.substr(0, slash);
    std::string path = slash == std::string::npos ? "" : rest.substr(slash + 1);
    size_t colon = auth.rfind(':');
    u.host = colon == std::string::npos ? auth : auth.substr(0, colon);
    if (colon != std::string::npos) u.port = auth.substr(colon + 1);
    size_t slash2 = path.find('/');
    u.app = slash2 == std::string::npos ? path : path.substr(0, slash2);
    u.stream = slash2 == std::string::npos ? "" : path.substr(slash2 + 1);
    u.tcUrl = "rtmp://" + u.host + "/" + u.app;
    return !u.host.empty() && !u.app.empty() && !u.stream.empty();
}

int connectTcp(const RtmpUrl& u) {
    addrinfo hints{}; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(u.host.c_str(), u.port.c_str(), &hints, &res) != 0) return -1;
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

bool sendAll(int fd, const std::vector<uint8_t>& data) {
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = send(fd, data.data() + off, data.size() - off, 0);
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

bool recvAll(int fd, std::vector<uint8_t>& data) {
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = recv(fd, data.data() + off, data.size() - off, 0);
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

void u16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(v >> 8); o.push_back(v & 0xff); }
void u24(std::vector<uint8_t>& o, uint32_t v) { o.push_back(v >> 16); o.push_back(v >> 8); o.push_back(v); }
void u32le(std::vector<uint8_t>& o, uint32_t v) { o.push_back(v); o.push_back(v >> 8); o.push_back(v >> 16); o.push_back(v >> 24); }

void amfString(std::vector<uint8_t>& o, const std::string& s) { o.push_back(2); u16(o, s.size()); o.insert(o.end(), s.begin(), s.end()); }
void amfNumber(std::vector<uint8_t>& o, double d) { o.push_back(0); uint64_t b; std::memcpy(&b, &d, 8); for (int i = 7; i >= 0; --i) o.push_back(b >> (i * 8)); }
void amfNull(std::vector<uint8_t>& o) { o.push_back(5); }
void amfBool(std::vector<uint8_t>& o, bool v) { o.push_back(1); o.push_back(v ? 1 : 0); }
void amfPropString(std::vector<uint8_t>& o, const std::string& k, const std::string& v) { u16(o, k.size()); o.insert(o.end(), k.begin(), k.end()); amfString(o, v); }
void amfPropNumber(std::vector<uint8_t>& o, const std::string& k, double v) { u16(o, k.size()); o.insert(o.end(), k.begin(), k.end()); amfNumber(o, v); }
void amfPropBool(std::vector<uint8_t>& o, const std::string& k, bool v) { u16(o, k.size()); o.insert(o.end(), k.begin(), k.end()); amfBool(o, v); }
void amfEnd(std::vector<uint8_t>& o) { o.push_back(0); o.push_back(0); o.push_back(9); }

std::vector<uint8_t> chunk(uint32_t sid, uint8_t type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> o; o.push_back(3); u24(o, 0); u24(o, payload.size()); o.push_back(type); u32le(o, sid); o.insert(o.end(), payload.begin(), payload.end()); return o;
}

std::vector<uint8_t> connectCmd(const RtmpUrl& u) {
    std::vector<uint8_t> p; amfString(p, "connect"); amfNumber(p, 1); p.push_back(3);
    amfPropString(p, "app", u.app); amfPropString(p, "type", "nonprivate"); amfPropString(p, "tcUrl", u.tcUrl);
    amfPropBool(p, "fpad", false); amfPropNumber(p, "capabilities", 15); amfPropNumber(p, "audioCodecs", 3575); amfPropNumber(p, "videoCodecs", 252); amfEnd(p);
    return chunk(0, 20, p);
}

std::vector<uint8_t> createStreamCmd() { std::vector<uint8_t> p; amfString(p, "createStream"); amfNumber(p, 2); amfNull(p); return chunk(0, 20, p); }
std::vector<uint8_t> playCmd(const std::string& stream) { std::vector<uint8_t> p; amfString(p, "play"); amfNumber(p, 3); amfNull(p); amfString(p, stream); return chunk(1, 20, p); }

bool handshake(int fd) {
    std::vector<uint8_t> c0c1(1537); c0c1[0] = 3;
    uint32_t seed = static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count());
    std::mt19937 rng(seed); for (size_t i = 9; i < c0c1.size(); ++i) c0c1[i] = rng();
    if (!sendAll(fd, c0c1)) return false;
    std::vector<uint8_t> s(3073); if (!recvAll(fd, s) || s[0] != 3) return false;
    std::vector<uint8_t> c2(s.begin() + 1, s.begin() + 1537);
    return sendAll(fd, c2);
}

void writeFlvHeader(std::ofstream& out) {
    const uint8_t h[] = {'F','L','V',1,5,0,0,0,9,0,0,0,0};
    out.write(reinterpret_cast<const char*>(h), sizeof(h));
}

void recordRawRtmpBytes(int fd, std::ofstream& out, int seconds) {
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    std::vector<uint8_t> buf(4096);
    while (std::chrono::steady_clock::now() < until) {
        ssize_t n = recv(fd, buf.data(), buf.size(), MSG_DONTWAIT);
        if (n > 0) {
            // 学习版：先保留原始 RTMP payload 观察。完整版需要按 chunk stream 拼接 message 再写 FLV Tag。
            out.write(reinterpret_cast<const char*>(buf.data()), n);
            std::cout << "received " << n << " bytes" << std::endl;
        } else {
            usleep(10000);
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " rtmp://host/app/stream output.flv [seconds]\n";
        return 1;
    }
    RtmpUrl url;
    if (!parseUrl(argv[1], url)) {
        std::cerr << "Invalid RTMP URL.\n";
        return 1;
    }
    int fd = connectTcp(url);
    if (fd < 0 || !handshake(fd)) {
        std::cerr << "RTMP connect/handshake failed.\n";
        return 1;
    }
    sendAll(fd, connectCmd(url));
    sendAll(fd, createStreamCmd());
    sendAll(fd, playCmd(url.stream));

    std::ofstream out(argv[2], std::ios::binary);
    writeFlvHeader(out);
    int seconds = argc >= 4 ? std::atoi(argv[3]) : 10;
    recordRawRtmpBytes(fd, out, seconds);
    close(fd);
    std::cout << "Learning capture saved to " << argv[2] << std::endl;
    return 0;
}
