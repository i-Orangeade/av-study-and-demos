extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <iostream>
#include <memory>
#include <string>

namespace {

struct StreamContext {
    int inputIndex = -1;
    AVMediaType type = AVMEDIA_TYPE_UNKNOWN;
    AVCodecContext* decoder = nullptr;
    AVCodecContext* encoder = nullptr;
    AVStream* inputStream = nullptr;
    AVStream* outputStream = nullptr;
    SwsContext* sws = nullptr;
    SwrContext* swr = nullptr;
    AVAudioFifo* fifo = nullptr;
    int64_t nextVideoPts = 0;
    int64_t nextAudioPts = 0;
};

struct PacketDeleter {
    void operator()(AVPacket* packet) const {
        av_packet_free(&packet);
    }
};

struct FrameDeleter {
    void operator()(AVFrame* frame) const {
        av_frame_free(&frame);
    }
};

using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

std::string fferr(int ret) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(ret, buffer, sizeof(buffer));
    return buffer;
}

void printUsage(const char* program) {
    std::cerr
        << "Usage:\n"
        << "  " << program << " <input-media> <output.mp4>\n\n"
        << "Example:\n"
        << "  " << program << " input.mkv output.mp4\n";
}

AVFrame* makeFrame(enum AVPixelFormat pixFmt, int width, int height) {
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        return nullptr;
    }

    frame->format = pixFmt;
    frame->width = width;
    frame->height = height;

    if (av_frame_get_buffer(frame, 32) < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    return frame;
}

AVFrame* makeAudioFrame(uint64_t channelLayout,
                        int channels,
                        enum AVSampleFormat sampleFmt,
                        int sampleRate,
                        int nbSamples) {
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        return nullptr;
    }

    frame->channel_layout = channelLayout;
    frame->channels = channels;
    frame->format = sampleFmt;
    frame->sample_rate = sampleRate;
    frame->nb_samples = nbSamples;

    if (nbSamples > 0 && av_frame_get_buffer(frame, 0) < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    return frame;
}

int encodeFrame(StreamContext& stream, AVFormatContext* outputFmt, AVFrame* frame) {
    int ret = avcodec_send_frame(stream.encoder, frame);
    if (ret < 0) {
        std::cerr << "avcodec_send_frame failed: " << fferr(ret) << std::endl;
        return ret;
    }

    PacketPtr packet(av_packet_alloc());
    if (!packet) {
        return AVERROR(ENOMEM);
    }

    while (true) {
        ret = avcodec_receive_packet(stream.encoder, packet.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        }
        if (ret < 0) {
            std::cerr << "avcodec_receive_packet failed: " << fferr(ret) << std::endl;
            return ret;
        }

        av_packet_rescale_ts(packet.get(), stream.encoder->time_base, stream.outputStream->time_base);
        packet->stream_index = stream.outputStream->index;

        ret = av_interleaved_write_frame(outputFmt, packet.get());
        av_packet_unref(packet.get());
        if (ret < 0) {
            std::cerr << "av_interleaved_write_frame failed: " << fferr(ret) << std::endl;
            return ret;
        }
    }
}

int encodeVideoFrame(StreamContext& stream, AVFormatContext* outputFmt, AVFrame* decoded) {
    FramePtr output(makeFrame(stream.encoder->pix_fmt, stream.encoder->width, stream.encoder->height));
    if (!output) {
        return AVERROR(ENOMEM);
    }

    int ret = av_frame_make_writable(output.get());
    if (ret < 0) {
        return ret;
    }

    sws_scale(stream.sws,
              decoded->data,
              decoded->linesize,
              0,
              stream.decoder->height,
              output->data,
              output->linesize);

    output->pts = stream.nextVideoPts++;
    return encodeFrame(stream, outputFmt, output.get());
}

int drainAudioFifo(StreamContext& stream, AVFormatContext* outputFmt, bool flushAll) {
    const int encoderFrameSize = stream.encoder->frame_size > 0 ? stream.encoder->frame_size : 1024;

    while (av_audio_fifo_size(stream.fifo) >= encoderFrameSize ||
           (flushAll && av_audio_fifo_size(stream.fifo) > 0)) {
        int nbSamples = flushAll && av_audio_fifo_size(stream.fifo) < encoderFrameSize
                            ? av_audio_fifo_size(stream.fifo)
                            : encoderFrameSize;

        FramePtr frame(makeAudioFrame(stream.encoder->channel_layout,
                                      stream.encoder->channels,
                                      stream.encoder->sample_fmt,
                                      stream.encoder->sample_rate,
                                      nbSamples));
        if (!frame) {
            return AVERROR(ENOMEM);
        }

        int ret = av_audio_fifo_read(stream.fifo,
                                     reinterpret_cast<void**>(frame->data),
                                     nbSamples);
        if (ret < nbSamples) {
            return AVERROR(EIO);
        }

        frame->pts = stream.nextAudioPts;
        stream.nextAudioPts += nbSamples;

        ret = encodeFrame(stream, outputFmt, frame.get());
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

int encodeAudioFrame(StreamContext& stream, AVFormatContext* outputFmt, AVFrame* decoded) {
    int dstSamples = static_cast<int>(av_rescale_rnd(
        swr_get_delay(stream.swr, stream.decoder->sample_rate) + decoded->nb_samples,
        stream.encoder->sample_rate,
        stream.decoder->sample_rate,
        AV_ROUND_UP));

    FramePtr converted(makeAudioFrame(stream.encoder->channel_layout,
                                      stream.encoder->channels,
                                      stream.encoder->sample_fmt,
                                      stream.encoder->sample_rate,
                                      dstSamples));
    if (!converted) {
        return AVERROR(ENOMEM);
    }

    int ret = swr_convert(stream.swr,
                          converted->data,
                          dstSamples,
                          const_cast<const uint8_t**>(decoded->extended_data),
                          decoded->nb_samples);
    if (ret < 0) {
        std::cerr << "swr_convert failed: " << fferr(ret) << std::endl;
        return ret;
    }

    converted->nb_samples = ret;

    ret = av_audio_fifo_realloc(stream.fifo,
                                av_audio_fifo_size(stream.fifo) + converted->nb_samples);
    if (ret < 0) {
        return ret;
    }

    ret = av_audio_fifo_write(stream.fifo,
                              reinterpret_cast<void**>(converted->data),
                              converted->nb_samples);
    if (ret < converted->nb_samples) {
        return AVERROR(EIO);
    }

    return drainAudioFifo(stream, outputFmt, false);
}

int processDecodedFrames(StreamContext& stream, AVFormatContext* outputFmt) {
    FramePtr frame(av_frame_alloc());
    if (!frame) {
        return AVERROR(ENOMEM);
    }

    while (true) {
        int ret = avcodec_receive_frame(stream.decoder, frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        }
        if (ret < 0) {
            std::cerr << "avcodec_receive_frame failed: " << fferr(ret) << std::endl;
            return ret;
        }

        ret = stream.type == AVMEDIA_TYPE_VIDEO
                  ? encodeVideoFrame(stream, outputFmt, frame.get())
                  : encodeAudioFrame(stream, outputFmt, frame.get());
        av_frame_unref(frame.get());

        if (ret < 0) {
            return ret;
        }
    }
}

int decodePacket(StreamContext& stream, AVFormatContext* outputFmt, AVPacket* packet) {
    int ret = avcodec_send_packet(stream.decoder, packet);
    if (ret < 0) {
        std::cerr << "avcodec_send_packet failed: " << fferr(ret) << std::endl;
        return ret;
    }

    return processDecodedFrames(stream, outputFmt);
}

int openDecoder(AVFormatContext* inputFmt, AVMediaType type, StreamContext& stream) {
    int index = av_find_best_stream(inputFmt, type, -1, -1, nullptr, 0);
    if (index < 0) {
        return index;
    }

    AVStream* inputStream = inputFmt->streams[index];
    const AVCodec* decoder = avcodec_find_decoder(inputStream->codecpar->codec_id);
    if (!decoder) {
        return AVERROR_DECODER_NOT_FOUND;
    }

    AVCodecContext* decoderCtx = avcodec_alloc_context3(decoder);
    if (!decoderCtx) {
        return AVERROR(ENOMEM);
    }

    int ret = avcodec_parameters_to_context(decoderCtx, inputStream->codecpar);
    if (ret < 0) {
        avcodec_free_context(&decoderCtx);
        return ret;
    }

    ret = avcodec_open2(decoderCtx, decoder, nullptr);
    if (ret < 0) {
        avcodec_free_context(&decoderCtx);
        return ret;
    }

    stream.inputIndex = index;
    stream.type = type;
    stream.inputStream = inputStream;
    stream.decoder = decoderCtx;
    return 0;
}

int openVideoEncoder(AVFormatContext* outputFmt, StreamContext& stream) {
    const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");
    if (!encoder) {
        encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    if (!encoder) {
        return AVERROR_ENCODER_NOT_FOUND;
    }

    AVStream* outputStream = avformat_new_stream(outputFmt, nullptr);
    if (!outputStream) {
        return AVERROR(ENOMEM);
    }

    AVCodecContext* encoderCtx = avcodec_alloc_context3(encoder);
    if (!encoderCtx) {
        return AVERROR(ENOMEM);
    }

    AVRational inputFps = av_guess_frame_rate(nullptr, stream.inputStream, nullptr);
    if (inputFps.num <= 0 || inputFps.den <= 0) {
        inputFps = AVRational{25, 1};
    }

    encoderCtx->width = stream.decoder->width;
    encoderCtx->height = stream.decoder->height;
    encoderCtx->sample_aspect_ratio = stream.decoder->sample_aspect_ratio;
    encoderCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    encoderCtx->time_base = av_inv_q(inputFps);
    encoderCtx->framerate = inputFps;
    encoderCtx->bit_rate = 2'000'000;
    encoderCtx->gop_size = 50;
    encoderCtx->max_b_frames = 2;

    if (outputFmt->oformat->flags & AVFMT_GLOBALHEADER) {
        encoderCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (std::string(encoder->name) == "libx264") {
        av_opt_set(encoderCtx->priv_data, "preset", "veryfast", 0);
    }

    int ret = avcodec_open2(encoderCtx, encoder, nullptr);
    if (ret < 0) {
        avcodec_free_context(&encoderCtx);
        return ret;
    }

    ret = avcodec_parameters_from_context(outputStream->codecpar, encoderCtx);
    if (ret < 0) {
        avcodec_free_context(&encoderCtx);
        return ret;
    }

    outputStream->time_base = encoderCtx->time_base;
    stream.encoder = encoderCtx;
    stream.outputStream = outputStream;
    stream.sws = sws_getContext(stream.decoder->width,
                                stream.decoder->height,
                                stream.decoder->pix_fmt,
                                encoderCtx->width,
                                encoderCtx->height,
                                encoderCtx->pix_fmt,
                                SWS_BILINEAR,
                                nullptr,
                                nullptr,
                                nullptr);
    return stream.sws ? 0 : AVERROR(EINVAL);
}

int openAudioEncoder(AVFormatContext* outputFmt, StreamContext& stream) {
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!encoder) {
        return AVERROR_ENCODER_NOT_FOUND;
    }

    AVStream* outputStream = avformat_new_stream(outputFmt, nullptr);
    if (!outputStream) {
        return AVERROR(ENOMEM);
    }

    AVCodecContext* encoderCtx = avcodec_alloc_context3(encoder);
    if (!encoderCtx) {
        return AVERROR(ENOMEM);
    }

    encoderCtx->sample_rate = stream.decoder->sample_rate > 0 ? stream.decoder->sample_rate : 48000;
    encoderCtx->sample_fmt = encoder->sample_fmts ? encoder->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    encoderCtx->bit_rate = 128000;
    encoderCtx->time_base = AVRational{1, encoderCtx->sample_rate};

    encoderCtx->channels = stream.decoder->channels > 0 ? stream.decoder->channels : 2;
    encoderCtx->channel_layout = stream.decoder->channel_layout;
    if (encoderCtx->channel_layout == 0) {
        encoderCtx->channel_layout = av_get_default_channel_layout(encoderCtx->channels);
    }

    if (outputFmt->oformat->flags & AVFMT_GLOBALHEADER) {
        encoderCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    int ret = avcodec_open2(encoderCtx, encoder, nullptr);
    if (ret < 0) {
        avcodec_free_context(&encoderCtx);
        return ret;
    }

    ret = avcodec_parameters_from_context(outputStream->codecpar, encoderCtx);
    if (ret < 0) {
        avcodec_free_context(&encoderCtx);
        return ret;
    }

    outputStream->time_base = encoderCtx->time_base;

    uint64_t inputChannelLayout = stream.decoder->channel_layout;
    if (inputChannelLayout == 0) {
        inputChannelLayout = av_get_default_channel_layout(stream.decoder->channels);
    }

    stream.swr = swr_alloc_set_opts(nullptr,
                                    static_cast<int64_t>(encoderCtx->channel_layout),
                                    encoderCtx->sample_fmt,
                                    encoderCtx->sample_rate,
                                    static_cast<int64_t>(inputChannelLayout),
                                    stream.decoder->sample_fmt,
                                    stream.decoder->sample_rate,
                                    0,
                                    nullptr);
    if (!stream.swr) {
        avcodec_free_context(&encoderCtx);
        return AVERROR(ENOMEM);
    }

    ret = swr_init(stream.swr);
    if (ret < 0) {
        avcodec_free_context(&encoderCtx);
        return ret;
    }

    stream.fifo = av_audio_fifo_alloc(encoderCtx->sample_fmt,
                                      encoderCtx->channels,
                                      1);
    if (!stream.fifo) {
        avcodec_free_context(&encoderCtx);
        return AVERROR(ENOMEM);
    }

    stream.encoder = encoderCtx;
    stream.outputStream = outputStream;
    return 0;
}

void cleanup(StreamContext& stream) {
    if (stream.fifo) {
        av_audio_fifo_free(stream.fifo);
        stream.fifo = nullptr;
    }
    if (stream.swr) {
        swr_free(&stream.swr);
    }
    if (stream.sws) {
        sws_freeContext(stream.sws);
        stream.sws = nullptr;
    }
    if (stream.encoder) {
        avcodec_free_context(&stream.encoder);
    }
    if (stream.decoder) {
        avcodec_free_context(&stream.decoder);
    }
}

int transcode(const std::string& inputPath, const std::string& outputPath) {
    AVFormatContext* inputFmt = nullptr;
    AVFormatContext* outputFmt = nullptr;
    StreamContext video;
    StreamContext audio;
    bool hasVideo = false;
    bool hasAudio = false;

    int ret = avformat_open_input(&inputFmt, inputPath.c_str(), nullptr, nullptr);
    if (ret < 0) {
        std::cerr << "avformat_open_input failed: " << fferr(ret) << std::endl;
        return 1;
    }

    ret = avformat_find_stream_info(inputFmt, nullptr);
    if (ret < 0) {
        std::cerr << "avformat_find_stream_info failed: " << fferr(ret) << std::endl;
        avformat_close_input(&inputFmt);
        return 1;
    }

    ret = avformat_alloc_output_context2(&outputFmt, nullptr, "mp4", outputPath.c_str());
    if (ret < 0 || !outputFmt) {
        std::cerr << "avformat_alloc_output_context2 failed: " << fferr(ret) << std::endl;
        avformat_close_input(&inputFmt);
        return 1;
    }

    if (openDecoder(inputFmt, AVMEDIA_TYPE_VIDEO, video) == 0) {
        ret = openVideoEncoder(outputFmt, video);
        if (ret < 0) {
            std::cerr << "open video encoder failed: " << fferr(ret) << std::endl;
            cleanup(video);
            avformat_free_context(outputFmt);
            avformat_close_input(&inputFmt);
            return 1;
        }
        hasVideo = true;
    }

    if (openDecoder(inputFmt, AVMEDIA_TYPE_AUDIO, audio) == 0) {
        ret = openAudioEncoder(outputFmt, audio);
        if (ret < 0) {
            std::cerr << "open audio encoder failed: " << fferr(ret) << std::endl;
            cleanup(video);
            cleanup(audio);
            avformat_free_context(outputFmt);
            avformat_close_input(&inputFmt);
            return 1;
        }
        hasAudio = true;
    }

    if (!hasVideo && !hasAudio) {
        std::cerr << "No audio or video stream found." << std::endl;
        cleanup(video);
        cleanup(audio);
        avformat_free_context(outputFmt);
        avformat_close_input(&inputFmt);
        return 1;
    }

    if (!(outputFmt->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outputFmt->pb, outputPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            std::cerr << "avio_open failed: " << fferr(ret) << std::endl;
            cleanup(video);
            cleanup(audio);
            avformat_free_context(outputFmt);
            avformat_close_input(&inputFmt);
            return 1;
        }
    }

    ret = avformat_write_header(outputFmt, nullptr);
    if (ret < 0) {
        std::cerr << "avformat_write_header failed: " << fferr(ret) << std::endl;
        cleanup(video);
        cleanup(audio);
        if (!(outputFmt->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&outputFmt->pb);
        }
        avformat_free_context(outputFmt);
        avformat_close_input(&inputFmt);
        return 1;
    }

    PacketPtr packet(av_packet_alloc());
    while ((ret = av_read_frame(inputFmt, packet.get())) >= 0) {
        if (hasVideo && packet->stream_index == video.inputIndex) {
            ret = decodePacket(video, outputFmt, packet.get());
        } else if (hasAudio && packet->stream_index == audio.inputIndex) {
            ret = decodePacket(audio, outputFmt, packet.get());
        } else {
            ret = 0;
        }

        av_packet_unref(packet.get());
        if (ret < 0) {
            break;
        }
    }

    if (ret == AVERROR_EOF) {
        ret = 0;
    }

    if (ret >= 0 && hasVideo) {
        ret = decodePacket(video, outputFmt, nullptr);
    }
    if (ret >= 0 && hasVideo) {
        ret = encodeFrame(video, outputFmt, nullptr);
    }
    if (ret >= 0 && hasAudio) {
        ret = decodePacket(audio, outputFmt, nullptr);
    }
    if (ret >= 0 && hasAudio) {
        ret = drainAudioFifo(audio, outputFmt, true);
    }
    if (ret >= 0 && hasAudio) {
        ret = encodeFrame(audio, outputFmt, nullptr);
    }

    if (ret >= 0) {
        ret = av_write_trailer(outputFmt);
    }

    cleanup(video);
    cleanup(audio);
    if (!(outputFmt->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&outputFmt->pb);
    }
    avformat_free_context(outputFmt);
    avformat_close_input(&inputFmt);

    if (ret < 0) {
        std::cerr << "Transcode failed: " << fferr(ret) << std::endl;
        return 1;
    }

    std::cout << "Saved H.264 + AAC MP4 to " << outputPath << std::endl;
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printUsage(argv[0]);
        return 1;
    }

    return transcode(argv[1], argv[2]);
}
