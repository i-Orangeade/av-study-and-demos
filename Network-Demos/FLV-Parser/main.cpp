#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

uint32_t readU24(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 16) |
           (static_cast<uint32_t>(p[1]) << 8) |
           static_cast<uint32_t>(p[2]);
}

uint32_t readU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

void writeStartCode(std::ofstream& out) {
    const uint8_t code[] = {0x00, 0x00, 0x00, 0x01};
    out.write(reinterpret_cast<const char*>(code), sizeof(code));
}

void writeAdtsHeader(std::ofstream& out, int aacProfile, int sampleRateIndex, int channelConfig, int payloadSize) {
    int frameLength = payloadSize + 7;
    uint8_t h[7] = {};
    h[0] = 0xFF;
    h[1] = 0xF1;
    h[2] = static_cast<uint8_t>(((aacProfile - 1) << 6) | (sampleRateIndex << 2) | (channelConfig >> 2));
    h[3] = static_cast<uint8_t>(((channelConfig & 3) << 6) | (frameLength >> 11));
    h[4] = static_cast<uint8_t>((frameLength >> 3) & 0xFF);
    h[5] = static_cast<uint8_t>(((frameLength & 7) << 5) | 0x1F);
    h[6] = 0xFC;
    out.write(reinterpret_cast<const char*>(h), sizeof(h));
}

bool readBytes(std::ifstream& in, std::vector<uint8_t>& data, size_t size) {
    data.resize(size);
    return static_cast<bool>(in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)));
}

void parseAvcVideo(const std::vector<uint8_t>& data, std::ofstream& h264) {
    if (data.size() < 5) return;
    uint8_t avcPacketType = data[1];
    const uint8_t* p = data.data() + 5;
    size_t remain = data.size() - 5;

    if (avcPacketType == 0) {
        if (remain < 7) return;
        uint8_t spsCount = p[5] & 0x1F;
        p += 6;
        remain -= 6;
        for (uint8_t i = 0; i < spsCount && remain >= 2; ++i) {
            uint16_t len = (p[0] << 8) | p[1];
            p += 2; remain -= 2;
            if (remain < len) return;
            writeStartCode(h264);
            h264.write(reinterpret_cast<const char*>(p), len);
            p += len; remain -= len;
        }
        if (remain < 1) return;
        uint8_t ppsCount = *p++;
        --remain;
        for (uint8_t i = 0; i < ppsCount && remain >= 2; ++i) {
            uint16_t len = (p[0] << 8) | p[1];
            p += 2; remain -= 2;
            if (remain < len) return;
            writeStartCode(h264);
            h264.write(reinterpret_cast<const char*>(p), len);
            p += len; remain -= len;
        }
        return;
    }

    if (avcPacketType == 1) {
        while (remain >= 4) {
            uint32_t len = readU32(p);
            p += 4; remain -= 4;
            if (remain < len) return;
            writeStartCode(h264);
            h264.write(reinterpret_cast<const char*>(p), len);
            p += len; remain -= len;
        }
    }
}

void parseAacAudio(const std::vector<uint8_t>& data,
                   std::ofstream& aac,
                   int& profile,
                   int& sampleRateIndex,
                   int& channelConfig) {
    if (data.size() < 2) return;
    uint8_t aacPacketType = data[1];
    if (aacPacketType == 0 && data.size() >= 4) {
        profile = (data[2] >> 3) & 0x1F;
        sampleRateIndex = ((data[2] & 0x07) << 1) | (data[3] >> 7);
        channelConfig = (data[3] >> 3) & 0x0F;
        std::cout << "[aac sequence] profile=" << profile
                  << " sampleRateIndex=" << sampleRateIndex
                  << " channels=" << channelConfig << std::endl;
        return;
    }

    if (aacPacketType == 1 && data.size() > 2) {
        const int payloadSize = static_cast<int>(data.size() - 2);
        writeAdtsHeader(aac, profile, sampleRateIndex, channelConfig, payloadSize);
        aac.write(reinterpret_cast<const char*>(data.data() + 2), payloadSize);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.flv> [out.h264] [out.aac]\n";
        return 1;
    }

    std::string h264Path = argc >= 3 ? argv[2] : "out.h264";
    std::string aacPath = argc >= 4 ? argv[3] : "out.aac";
    std::ifstream in(argv[1], std::ios::binary);
    std::ofstream h264(h264Path, std::ios::binary);
    std::ofstream aac(aacPath, std::ios::binary);
    if (!in || !h264 || !aac) {
        std::cerr << "Failed to open input or output files.\n";
        return 1;
    }

    std::vector<uint8_t> header;
    if (!readBytes(in, header, 9) || header[0] != 'F' || header[1] != 'L' || header[2] != 'V') {
        std::cerr << "Invalid FLV header.\n";
        return 1;
    }

    std::cout << "FLV version=" << static_cast<int>(header[3])
              << " hasAudio=" << ((header[4] & 0x04) != 0)
              << " hasVideo=" << ((header[4] & 0x01) != 0) << std::endl;

    in.ignore(4);  // PreviousTagSize0
    int aacProfile = 2;
    int sampleRateIndex = 4;
    int channelConfig = 2;
    uint64_t tagIndex = 0;

    while (true) {
        std::vector<uint8_t> tagHeader;
        if (!readBytes(in, tagHeader, 11)) break;
        uint8_t tagType = tagHeader[0];
        uint32_t dataSize = readU24(tagHeader.data() + 1);
        uint32_t timestamp = readU24(tagHeader.data() + 4) | (static_cast<uint32_t>(tagHeader[7]) << 24);

        std::vector<uint8_t> data;
        if (!readBytes(in, data, dataSize)) break;
        in.ignore(4);  // PreviousTagSize

        std::cout << "tag=" << tagIndex++
                  << " type=" << static_cast<int>(tagType)
                  << " timestamp=" << timestamp
                  << " size=" << dataSize << std::endl;

        if (tagType == 9) {
            parseAvcVideo(data, h264);
        } else if (tagType == 8) {
            parseAacAudio(data, aac, aacProfile, sampleRateIndex, channelConfig);
        }
    }

    std::cout << "Wrote " << h264Path << " and " << aacPath << std::endl;
    return 0;
}
