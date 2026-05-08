#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

namespace {

struct FormatContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) {
            avformat_close_input(&ctx);
        }
    }
};

struct PacketDeleter {
    void operator()(AVPacket* packet) const {
        if (packet) {
            av_packet_free(&packet);
        }
    }
};

struct BsfContextDeleter {
    void operator()(AVBSFContext* ctx) const {
        if (ctx) {
            av_bsf_free(&ctx);
        }
    }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using BsfContextPtr = std::unique_ptr<AVBSFContext, BsfContextDeleter>;

std::string ffmpegError(int errnum) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errnum, buffer, sizeof(buffer));
    return std::string(buffer);
}

void throwIfError(int ret, const std::string& message) {
    if (ret < 0) {
        throw std::runtime_error(message + ": " + ffmpegError(ret));
    }
}

int findStream(AVFormatContext* fmt, AVMediaType type, AVCodecID codecId) {
    for (unsigned int i = 0; i < fmt->nb_streams; ++i) {
        const AVCodecParameters* params = fmt->streams[i]->codecpar;
        if (params->codec_type == type && params->codec_id == codecId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int sampleRateIndex(int sampleRate) {
    static const int sampleRates[] = {
        96000, 88200, 64000, 48000, 44100, 32000, 24000,
        22050, 16000, 12000, 11025, 8000, 7350
    };

    for (int i = 0; i < static_cast<int>(sizeof(sampleRates) / sizeof(sampleRates[0])); ++i) {
        if (sampleRates[i] == sampleRate) {
            return i;
        }
    }

    return -1;
}

uint32_t readBits(const uint8_t* data, int bitOffset, int bitCount) {
    uint32_t value = 0;
    for (int i = 0; i < bitCount; ++i) {
        const int byteIndex = (bitOffset + i) / 8;
        const int bitIndex = 7 - ((bitOffset + i) % 8);
        value = (value << 1) | ((data[byteIndex] >> bitIndex) & 0x01);
    }
    return value;
}

struct AacConfig {
    int profile = 1;       // ADTS profile: AAC LC is 1.
    int sampleRateIndex = -1;
    int channelConfig = 0;
};

AacConfig parseAacConfig(const AVCodecParameters* params) {
    AacConfig config;

    if (params->extradata && params->extradata_size >= 2) {
        const int audioObjectType = static_cast<int>(readBits(params->extradata, 0, 5));
        int frequencyIndex = static_cast<int>(readBits(params->extradata, 5, 4));
        int channelConfig = static_cast<int>(readBits(params->extradata, 9, 4));

        if (frequencyIndex == 0x0f && params->extradata_size >= 5) {
            frequencyIndex = sampleRateIndex(static_cast<int>(readBits(params->extradata, 9, 24)));
            channelConfig = static_cast<int>(readBits(params->extradata, 33, 4));
        }

        config.profile = audioObjectType > 0 ? audioObjectType - 1 : 1;
        if (config.profile < 0 || config.profile > 3) {
            config.profile = 1;
        }
        config.sampleRateIndex = frequencyIndex;
        config.channelConfig = channelConfig;
    }

    if (config.sampleRateIndex < 0 || config.sampleRateIndex > 12) {
        config.sampleRateIndex = sampleRateIndex(params->sample_rate);
    }

    if (config.channelConfig <= 0) {
        config.channelConfig = params->channels > 0 ? params->channels : 2;
    }

    if (config.sampleRateIndex < 0) {
        throw std::runtime_error("Unsupported AAC sample rate: " + std::to_string(params->sample_rate));
    }

    return config;
}

std::array<uint8_t, 7> makeAdtsHeader(const AacConfig& config, int payloadSize) {
    const int frameLength = payloadSize + 7;
    std::array<uint8_t, 7> header{};

    header[0] = 0xff;
    header[1] = 0xf1;
    header[2] = static_cast<uint8_t>(((config.profile & 0x03) << 6) |
                                     ((config.sampleRateIndex & 0x0f) << 2) |
                                     ((config.channelConfig >> 2) & 0x01));
    header[3] = static_cast<uint8_t>(((config.channelConfig & 0x03) << 6) |
                                     ((frameLength >> 11) & 0x03));
    header[4] = static_cast<uint8_t>((frameLength >> 3) & 0xff);
    header[5] = static_cast<uint8_t>(((frameLength & 0x07) << 5) | 0x1f);
    header[6] = 0xfc;

    return header;
}

BsfContextPtr createH264Bsf(const AVCodecParameters* params, AVRational timeBase) {
    const AVBitStreamFilter* filter = av_bsf_get_by_name("h264_mp4toannexb");
    if (!filter) {
        throw std::runtime_error("Cannot find h264_mp4toannexb bitstream filter");
    }

    AVBSFContext* rawBsf = nullptr;
    throwIfError(av_bsf_alloc(filter, &rawBsf), "av_bsf_alloc failed");
    BsfContextPtr bsf(rawBsf);

    throwIfError(avcodec_parameters_copy(bsf->par_in, params), "avcodec_parameters_copy failed");
    bsf->time_base_in = timeBase;
    throwIfError(av_bsf_init(bsf.get()), "av_bsf_init failed");

    return bsf;
}

void drainH264Bsf(AVBSFContext* bsf, std::ofstream& output) {
    PacketPtr filtered(av_packet_alloc());
    if (!filtered) {
        throw std::runtime_error("av_packet_alloc failed");
    }

    while (true) {
        const int ret = av_bsf_receive_packet(bsf, filtered.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        throwIfError(ret, "av_bsf_receive_packet failed");

        output.write(reinterpret_cast<const char*>(filtered->data), filtered->size);
        av_packet_unref(filtered.get());
    }
}

void printUsage(const char* program) {
    std::cerr << "Usage:\n"
              << "  " << program << " <input.mp4> <output.h264> <output.aac>\n\n"
              << "Example:\n"
              << "  " << program << " input.mp4 out.h264 out.aac\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 4) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string inputPath = argv[1];
    const std::string h264Path = argv[2];
    const std::string aacPath = argv[3];

    try {
        AVFormatContext* rawFmt = nullptr;
        throwIfError(avformat_open_input(&rawFmt, inputPath.c_str(), nullptr, nullptr),
                     "avformat_open_input failed");
        FormatContextPtr fmt(rawFmt);

        throwIfError(avformat_find_stream_info(fmt.get(), nullptr), "avformat_find_stream_info failed");

        const int videoStreamIndex = findStream(fmt.get(), AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_H264);
        const int audioStreamIndex = findStream(fmt.get(), AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_AAC);

        if (videoStreamIndex < 0 && audioStreamIndex < 0) {
            throw std::runtime_error("No H.264 video stream or AAC audio stream found");
        }

        std::ofstream h264Output;
        BsfContextPtr h264Bsf;
        if (videoStreamIndex >= 0) {
            h264Output.open(h264Path, std::ios::binary);
            if (!h264Output) {
                throw std::runtime_error("Cannot open H.264 output file: " + h264Path);
            }
            h264Bsf = createH264Bsf(fmt->streams[videoStreamIndex]->codecpar,
                                    fmt->streams[videoStreamIndex]->time_base);
            std::cout << "H.264 stream index: " << videoStreamIndex << "\n";
        } else {
            std::cout << "No H.264 stream found, skip video output.\n";
        }

        std::ofstream aacOutput;
        AacConfig aacConfig;
        if (audioStreamIndex >= 0) {
            aacOutput.open(aacPath, std::ios::binary);
            if (!aacOutput) {
                throw std::runtime_error("Cannot open AAC output file: " + aacPath);
            }
            aacConfig = parseAacConfig(fmt->streams[audioStreamIndex]->codecpar);
            std::cout << "AAC stream index: " << audioStreamIndex << "\n";
        } else {
            std::cout << "No AAC stream found, skip audio output.\n";
        }

        PacketPtr packet(av_packet_alloc());
        if (!packet) {
            throw std::runtime_error("av_packet_alloc failed");
        }

        int64_t videoPacketCount = 0;
        int64_t audioPacketCount = 0;

        while (av_read_frame(fmt.get(), packet.get()) >= 0) {
            if (packet->stream_index == videoStreamIndex && h264Bsf) {
                throwIfError(av_bsf_send_packet(h264Bsf.get(), packet.get()), "av_bsf_send_packet failed");
                drainH264Bsf(h264Bsf.get(), h264Output);
                ++videoPacketCount;
            } else if (packet->stream_index == audioStreamIndex && aacOutput) {
                const auto header = makeAdtsHeader(aacConfig, packet->size);
                aacOutput.write(reinterpret_cast<const char*>(header.data()), header.size());
                aacOutput.write(reinterpret_cast<const char*>(packet->data), packet->size);
                ++audioPacketCount;
            }

            av_packet_unref(packet.get());
        }

        if (h264Bsf) {
            throwIfError(av_bsf_send_packet(h264Bsf.get(), nullptr), "av_bsf_send_packet flush failed");
            drainH264Bsf(h264Bsf.get(), h264Output);
        }

        std::cout << "Done.\n"
                  << "Video packets extracted: " << videoPacketCount << "\n"
                  << "Audio packets extracted: " << audioPacketCount << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
