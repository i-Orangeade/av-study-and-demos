#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct RtmpUrl {
    std::string host;
    std::string port = "1935";
    std::string app;
    std::string stream;
    std::string tcUrl;
};

bool parseRtmpUrl(const std::string& input, RtmpUrl& url) {
    const std::string prefix = "rtmp://";
    if (input.rfind(prefix, 0) != 0) return false;
    std::string rest = input.substr(prefix.size());
    size_t slash = rest.find('/');
    std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    std::string path = slash == std::string::npos ? "" : rest.substr(slash + 1);
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        url.host = authority.substr(0, colon);
        url.port = authority.substr(colon + 1);
    } else {
        url.host = authority;
    }
    size_t next = path.find('/');
    url.app = next == std::string::npos ? path : path.substr(0, next);
    url.stream = next == std::string::npos ? "" : path.substr(next + 1);
    url.tcUrl = "rtmp://" + url.host + "/" + url.app;
    return !url.host.empty() && !url.app.empty();
}

int connectTcp(const std::string& host, const std::string& port) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &results) != 0) return -1;
    int fd = -1;
    for (addrinfo* p = results; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd >= 0 && connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        if (fd >= 0) close(fd);
        fd = -1;
    }
    freeaddrinfo(results);
    return fd;
}

bool sendAll(int fd, const std::vector<uint8_t>& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool recvAll(int fd, std::vector<uint8_t>& data) {
    size_t got = 0;
    while (got < data.size()) {
        ssize_t n = recv(fd, data.data() + got, data.size() - got, 0);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

void putU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(v >> 8);
    out.push_back(v & 0xFF);
}

void putU24(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >> 8) & 0xFF);
    out.push_back(v & 0xFF);
}

void putU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(v & 0xFF);
    out.push_back((v >> 8) & 0xFF);
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >> 24) & 0xFF);
}

void amfString(std::vector<uint8_t>& out, const std::string& s) {
    out.push_back(0x02);
    putU16(out, static_cast<uint16_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

void amfNumber(std::vector<uint8_t>& out, double value) {
    out.push_back(0x00);
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 7; i >= 0; --i) out.push_back((bits >> (i * 8)) & 0xFF);
}

void amfBool(std::vector<uint8_t>& out, bool v) {
    out.push_back(0x01);
    out.push_back(v ? 1 : 0);
}

void amfNull(std::vector<uint8_t>& out) {
    out.push_back(0x05);
}

void amfObjectEnd(std::vector<uint8_t>& out) {
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x09);
}

void amfPropString(std::vector<uint8_t>& out, const std::string& key, const std::string& value) {
    putU16(out, static_cast<uint16_t>(key.size()));
    out.insert(out.end(), key.begin(), key.end());
    amfString(out, value);
}

void amfPropNumber(std::vector<uint8_t>& out, const std::string& key, double value) {
    putU16(out, static_cast<uint16_t>(key.size()));
    out.insert(out.end(), key.begin(), key.end());
    amfNumber(out, value);
}

void amfPropBool(std::vector<uint8_t>& out, const std::string& key, bool value) {
    putU16(out, static_cast<uint16_t>(key.size()));
    out.insert(out.end(), key.begin(), key.end());
    amfBool(out, value);
}

std::vector<uint8_t> makeCommandChunk(uint32_t timestamp,
                                      uint32_t streamId,
                                      uint8_t typeId,
                                      const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    out.push_back(0x03);  // fmt=0, csid=3
    putU24(out, timestamp);
    putU24(out, static_cast<uint32_t>(payload.size()));
    out.push_back(typeId);
    putU32LE(out, streamId);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<uint8_t> makeConnect(const RtmpUrl& url) {
    std::vector<uint8_t> p;
    amfString(p, "connect");
    amfNumber(p, 1);
    p.push_back(0x03);  // object
    amfPropString(p, "app", url.app);
    amfPropString(p, "type", "nonprivate");
    amfPropString(p, "tcUrl", url.tcUrl);
    amfPropBool(p, "fpad", false);
    amfPropNumber(p, "capabilities", 15);
    amfPropNumber(p, "audioCodecs", 3575);
    amfPropNumber(p, "videoCodecs", 252);
    amfPropNumber(p, "videoFunction", 1);
    amfObjectEnd(p);
    return makeCommandChunk(0, 0, 20, p);
}

std::vector<uint8_t> makeCreateStream() {
    std::vector<uint8_t> p;
    amfString(p, "createStream");
    amfNumber(p, 2);
    amfNull(p);
    return makeCommandChunk(0, 0, 20, p);
}

bool rtmpHandshake(int fd) {
    std::vector<uint8_t> c0c1(1537);
    c0c1[0] = 3;
    uint32_t now = static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count());
    c0c1[1] = (now >> 24) & 0xFF;
    c0c1[2] = (now >> 16) & 0xFF;
    c0c1[3] = (now >> 8) & 0xFF;
    c0c1[4] = now & 0xFF;
    std::mt19937 rng(now);
    for (size_t i = 9; i < c0c1.size(); ++i) c0c1[i] = static_cast<uint8_t>(rng());
    if (!sendAll(fd, c0c1)) return false;

    std::vector<uint8_t> s0s1s2(3073);
    if (!recvAll(fd, s0s1s2)) return false;
    if (s0s1s2[0] != 3) {
        std::cerr << "Unexpected RTMP version: " << static_cast<int>(s0s1s2[0]) << std::endl;
        return false;
    }

    std::vector<uint8_t> c2(s0s1s2.begin() + 1, s0s1s2.begin() + 1537);
    return sendAll(fd, c2);
}

void readSomeChunks(int fd) {
    std::vector<uint8_t> buf(4096);
    ssize_t n = recv(fd, buf.data(), buf.size(), 0);
    if (n <= 0) {
        std::cout << "No RTMP response payload received yet." << std::endl;
        return;
    }
    std::cout << "Received " << n << " bytes after command exchange." << std::endl;
    if (n >= 12) {
        uint8_t basic = buf[0];
        uint8_t fmt = basic >> 6;
        uint32_t csid = basic & 0x3F;
        uint32_t msgLen = (buf[4] << 16) | (buf[5] << 8) | buf[6];
        uint8_t typeId = buf[7];
        std::cout << "First chunk: fmt=" << static_cast<int>(fmt)
                  << " csid=" << csid
                  << " type=" << static_cast<int>(typeId)
                  << " messageLength=" << msgLen << std::endl;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " rtmp://host[:port]/app[/stream]\n";
        return 1;
    }

    RtmpUrl url;
    if (!parseRtmpUrl(argv[1], url)) {
        std::cerr << "Invalid RTMP URL.\n";
        return 1;
    }

    int fd = connectTcp(url.host, url.port);
    if (fd < 0) {
        std::cerr << "TCP connect failed.\n";
        return 1;
    }
    std::cout << "TCP connected to " << url.host << ":" << url.port << std::endl;

    if (!rtmpHandshake(fd)) {
        std::cerr << "RTMP handshake failed.\n";
        close(fd);
        return 1;
    }
    std::cout << "RTMP handshake OK." << std::endl;

    sendAll(fd, makeConnect(url));
    sendAll(fd, makeCreateStream());
    readSomeChunks(fd);
    close(fd);
    return 0;
}
