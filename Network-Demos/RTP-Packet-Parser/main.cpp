#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

void startCode(std::ofstream& out) {
    const uint8_t code[] = {0, 0, 0, 1};
    out.write(reinterpret_cast<const char*>(code), sizeof(code));
}

uint16_t u16(const uint8_t* p) { return (p[0] << 8) | p[1]; }
uint32_t u32(const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

void handleH264Payload(const uint8_t* p, size_t n, std::ofstream& out) {
    if (n == 0) return;
    uint8_t nalType = p[0] & 0x1F;
    if (nalType >= 1 && nalType <= 23) {
        startCode(out);
        out.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(n));
        return;
    }

    if (nalType == 28 && n >= 2) {  // FU-A
        uint8_t fuIndicator = p[0];
        uint8_t fuHeader = p[1];
        bool start = fuHeader & 0x80;
        bool end = fuHeader & 0x40;
        uint8_t reconstructed = (fuIndicator & 0xE0) | (fuHeader & 0x1F);
        if (start) {
            startCode(out);
            out.put(static_cast<char>(reconstructed));
        }
        out.write(reinterpret_cast<const char*>(p + 2), static_cast<std::streamsize>(n - 2));
        if (end) out.flush();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <udp-port> <out.h264>\n";
        return 1;
    }
    int port = std::atoi(argv[1]);
    std::ofstream out(argv[2], std::ios::binary);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (!out || fd < 0) {
        std::cerr << "Failed to open output or socket.\n";
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    std::cout << "Listening RTP/H264 on UDP port " << port << std::endl;
    std::vector<uint8_t> packet(2048);
    uint16_t lastSeq = 0;
    bool haveSeq = false;

    while (true) {
        ssize_t n = recv(fd, packet.data(), packet.size(), 0);
        if (n < 12) continue;
        uint8_t version = packet[0] >> 6;
        uint8_t cc = packet[0] & 0x0F;
        bool marker = packet[1] & 0x80;
        uint8_t payloadType = packet[1] & 0x7F;
        uint16_t seq = u16(packet.data() + 2);
        uint32_t timestamp = u32(packet.data() + 4);
        size_t headerSize = 12 + cc * 4;
        if (version != 2 || static_cast<size_t>(n) < headerSize) continue;

        if (haveSeq && static_cast<uint16_t>(lastSeq + 1) != seq) {
            std::cout << "sequence gap: last=" << lastSeq << " current=" << seq << std::endl;
        }
        haveSeq = true;
        lastSeq = seq;

        std::cout << "rtp seq=" << seq << " ts=" << timestamp
                  << " pt=" << static_cast<int>(payloadType)
                  << " marker=" << marker
                  << " payload=" << (n - static_cast<ssize_t>(headerSize)) << std::endl;

        handleH264Payload(packet.data() + headerSize, static_cast<size_t>(n) - headerSize, out);
    }
}
