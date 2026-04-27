#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <cstdint>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cmath>
#include <memory>

// FFmpeg 头文件
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/version.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
}

// SDL 头文件
#include <SDL2/SDL.h>

static std::string ff_err2str(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

// -------------------------- 配置参数 --------------------------
#define MAX_QUEUE_SIZE 100    // 音视频帧队列最大长度
#define SYNC_THRESHOLD 0.05   // 音视频同步阈值（秒）

// -------------------------- 智能指针删除器 --------------------------
struct AVFrameDeleter {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};
struct AVPacketDeleter {
    void operator()(AVPacket* pkt) const { av_packet_free(&pkt); }
};
struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const { avcodec_free_context(&ctx); }
};
struct AVFormatContextDeleter {
    void operator()(AVFormatContext* ctx) const { avformat_close_input(&ctx); }
};
struct SwsContextDeleter {
    void operator()(SwsContext* ctx) const { sws_freeContext(ctx); }
};
struct SwrContextDeleter {
    void operator()(SwrContext* ctx) const { swr_free(&ctx); }
};

using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using AVFormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

// -------------------------- 队列（线程安全，可中止） --------------------------
template<typename T>
class FrameQueue {
private:
    std::queue<T> queue;
    std::mutex mtx;
    std::condition_variable cv;
    bool abort = false;
public:
    void push(T&& frame) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]() { return abort || queue.size() < MAX_QUEUE_SIZE; });
        if (abort) return;
        queue.push(std::forward<T>(frame));
        cv.notify_one();
    }

    bool pop(T& frame) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]() { return abort || !queue.empty(); });
        if (abort) return false;
        if (queue.empty()) return false;
        frame = std::move(queue.front());
        queue.pop();
        cv.notify_one();
        return true;
    }

    bool try_pop(T& frame) {
        std::lock_guard<std::mutex> lock(mtx);
        if (abort || queue.empty()) return false;
        frame = std::move(queue.front());
        queue.pop();
        cv.notify_one();
        return true;
    }

    bool isEmpty() {
        std::lock_guard<std::mutex> lock(mtx);
        return queue.empty();
    }

    void setAbort() {
        std::lock_guard<std::mutex> lock(mtx);
        abort = true;
        cv.notify_all();
    }
};

// -------------------------- 全局播放器状态 --------------------------
struct PlayerState {
    // 解封装上下文
    AVFormatContextPtr fmt_ctx{nullptr};

    // 视频
    int video_stream_idx = -1;
    AVCodecContextPtr video_ctx{nullptr};
    FrameQueue<AVFramePtr> video_queue;
    double video_clock = 0.0;  // 视频时钟

    // 音频
    int audio_stream_idx = -1;
    AVCodecContextPtr audio_ctx{nullptr};
    SwrContextPtr swr_ctx{nullptr};
    AVAudioFifo* audio_fifo = nullptr; // S16 packed
    std::mutex audio_fifo_mtx;
    int out_channels = 2;
    int out_sample_rate = 0;
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;

    // 音频时钟（以“实际送入 SDL 的样本数”推进）
    std::mutex audio_clock_mtx;
    double audio_start_pts_sec = 0.0;
    bool audio_start_pts_valid = false;
    int64_t audio_played_samples = 0; // 每声道
    std::atomic<double> audio_clock{0.0};  // 同步基准

    // 像素格式转换
    SwsContextPtr sws_ctx{nullptr};
    AVFramePtr rgb_frame{nullptr};

    // SDL
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_AudioDeviceID audio_dev = 0;

    // 控制
    std::atomic<bool> quit{false};
} g_player;

static inline double ts_to_sec(int64_t ts, AVRational tb) {
    if (ts == AV_NOPTS_VALUE) return NAN;
    return (double)ts * av_q2d(tb);
}

// -------------------------- 音频回调（SDL 拉取音频：不做解码/重采样） --------------------------
void audio_callback(void* userdata, Uint8* stream, int len) {
    PlayerState* player = (PlayerState*)userdata;
    SDL_memset(stream, 0, len);

    const int bytes_per_sample = av_get_bytes_per_sample(player->out_sample_fmt);
    const int frame_bytes = player->out_channels * bytes_per_sample;
    if (frame_bytes <= 0) return;

    const int requested_samples = len / frame_bytes; // 每声道
    if (requested_samples <= 0) return;

    static thread_local std::vector<uint8_t> tmp;
    tmp.resize((size_t)requested_samples * frame_bytes);

    int got_samples = 0;
    {
        std::lock_guard<std::mutex> lock(player->audio_fifo_mtx);
        if (player->audio_fifo) {
            int available = av_audio_fifo_size(player->audio_fifo);
            int to_read = std::min(available, requested_samples);
            if (to_read > 0) {
                void* out_data[1] = { tmp.data() };
                got_samples = av_audio_fifo_read(player->audio_fifo, out_data, to_read);
            }
        }
    }

    if (got_samples > 0) {
        SDL_MixAudioFormat(stream, tmp.data(), AUDIO_S16SYS, got_samples * frame_bytes, SDL_MIX_MAXVOLUME);
    }

    // 即使 FIFO 欠载，SDL 也会播放“静音”——时钟仍应前进，否则视频会一直等待而“卡住”
    {
        std::lock_guard<std::mutex> lock(player->audio_clock_mtx);
        player->audio_played_samples += requested_samples;
        if (player->audio_start_pts_valid && player->out_sample_rate > 0) {
            double clk = player->audio_start_pts_sec +
                         (double)player->audio_played_samples / (double)player->out_sample_rate;
            player->audio_clock.store(clk, std::memory_order_relaxed);
        }
    }
}

// -------------------------- 初始化音频 --------------------------
bool init_audio() {
    AVCodecParameters* params = g_player.fmt_ctx->streams[g_player.audio_stream_idx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec) {
        std::cerr << "音频: 找不到解码器 codec_id=" << params->codec_id << std::endl;
        return false;
    }

    g_player.audio_ctx = AVCodecContextPtr(avcodec_alloc_context3(codec));
    if (!g_player.audio_ctx) {
        std::cerr << "音频: avcodec_alloc_context3 失败" << std::endl;
        return false;
    }
    int ret = avcodec_parameters_to_context(g_player.audio_ctx.get(), params);
    if (ret < 0) {
        std::cerr << "音频: avcodec_parameters_to_context 失败: " << ff_err2str(ret) << std::endl;
        return false;
    }
    ret = avcodec_open2(g_player.audio_ctx.get(), codec, nullptr);
    if (ret < 0) {
        std::cerr << "音频: avcodec_open2 失败: " << ff_err2str(ret) << std::endl;
        return false;
    }

    g_player.out_sample_rate = g_player.audio_ctx->sample_rate > 0 ? g_player.audio_ctx->sample_rate : 48000;

    // 重采样配置：AAC(常见 FLTP) -> S16SYS。
#if LIBAVUTIL_VERSION_MAJOR >= 57
    AVChannelLayout out_ch_layout;
    av_channel_layout_default(&out_ch_layout, g_player.out_channels);

    AVChannelLayout in_ch_layout = {};
    if (g_player.audio_ctx->ch_layout.nb_channels > 0) {
        if (av_channel_layout_copy(&in_ch_layout, &g_player.audio_ctx->ch_layout) < 0) {
            av_channel_layout_uninit(&out_ch_layout);
            return false;
        }
    } else {
        av_channel_layout_default(&in_ch_layout, g_player.out_channels);
    }

    SwrContext* swr = nullptr;
    if (swr_alloc_set_opts2(
            &swr,
            &out_ch_layout,
            g_player.out_sample_fmt,
            g_player.out_sample_rate,
            &in_ch_layout,
            g_player.audio_ctx->sample_fmt,
            g_player.audio_ctx->sample_rate,
            0,
            nullptr) < 0) {
        av_channel_layout_uninit(&in_ch_layout);
        av_channel_layout_uninit(&out_ch_layout);
        return false;
    }

    g_player.swr_ctx = SwrContextPtr(swr);
    if (swr_init(g_player.swr_ctx.get()) < 0) {
        av_channel_layout_uninit(&in_ch_layout);
        av_channel_layout_uninit(&out_ch_layout);
        return false;
    }

    av_channel_layout_uninit(&in_ch_layout);
    av_channel_layout_uninit(&out_ch_layout);
#else
    uint64_t out_ch_layout = av_get_default_channel_layout(g_player.out_channels);
    uint64_t in_ch_layout = g_player.audio_ctx->channel_layout;
    if (in_ch_layout == 0) {
        int in_channels = g_player.audio_ctx->channels > 0 ? g_player.audio_ctx->channels : g_player.out_channels;
        in_ch_layout = av_get_default_channel_layout(in_channels);
    }

    SwrContext* swr = swr_alloc_set_opts(
        nullptr,
        static_cast<int64_t>(out_ch_layout),
        g_player.out_sample_fmt,
        g_player.out_sample_rate,
        static_cast<int64_t>(in_ch_layout),
        g_player.audio_ctx->sample_fmt,
        g_player.audio_ctx->sample_rate,
        0,
        nullptr
    );
    if (!swr) return false;

    g_player.swr_ctx = SwrContextPtr(swr);
    if (swr_init(g_player.swr_ctx.get()) < 0) return false;
#endif

    g_player.audio_fifo = av_audio_fifo_alloc(g_player.out_sample_fmt, g_player.out_channels, 1);
    if (!g_player.audio_fifo) {
        std::cerr << "音频: av_audio_fifo_alloc 失败" << std::endl;
        return false;
    }

    // SDL 音频
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = g_player.out_sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = (Uint8)g_player.out_channels;
    want.samples = 4096;
    want.callback = audio_callback;
    want.userdata = &g_player;

    g_player.audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_player.audio_dev == 0) {
        std::cerr << "音频: SDL_OpenAudioDevice 失败: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_PauseAudioDevice(g_player.audio_dev, 0);
    return true;
}

// -------------------------- 初始化视频 --------------------------
bool init_video() {
    AVCodecParameters* params = g_player.fmt_ctx->streams[g_player.video_stream_idx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec) {
        std::cerr << "视频: 找不到解码器 codec_id=" << params->codec_id << std::endl;
        return false;
    }

    g_player.video_ctx = AVCodecContextPtr(avcodec_alloc_context3(codec));
    if (!g_player.video_ctx) {
        std::cerr << "视频: avcodec_alloc_context3 失败" << std::endl;
        return false;
    }
    int ret = avcodec_parameters_to_context(g_player.video_ctx.get(), params);
    if (ret < 0) {
        std::cerr << "视频: avcodec_parameters_to_context 失败: " << ff_err2str(ret) << std::endl;
        return false;
    }
    ret = avcodec_open2(g_player.video_ctx.get(), codec, nullptr);
    if (ret < 0) {
        std::cerr << "视频: avcodec_open2 失败: " << ff_err2str(ret) << std::endl;
        return false;
    }

    // 像素格式转换：YUV420P → RGB24
    g_player.sws_ctx = SwsContextPtr(sws_getContext(
        g_player.video_ctx->width, g_player.video_ctx->height, g_player.video_ctx->pix_fmt,
        g_player.video_ctx->width, g_player.video_ctx->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    ));
    if (!g_player.sws_ctx) {
        std::cerr << "视频: sws_getContext 失败 (pix_fmt=" << g_player.video_ctx->pix_fmt << ")" << std::endl;
        return false;
    }

    // RGB 帧
    g_player.rgb_frame = AVFramePtr(av_frame_alloc());
    if (!g_player.rgb_frame) {
        std::cerr << "视频: av_frame_alloc 失败" << std::endl;
        return false;
    }
    g_player.rgb_frame->format = AV_PIX_FMT_RGB24;
    g_player.rgb_frame->width = g_player.video_ctx->width;
    g_player.rgb_frame->height = g_player.video_ctx->height;
    ret = av_frame_get_buffer(g_player.rgb_frame.get(), 0);
    if (ret < 0) {
        std::cerr << "视频: av_frame_get_buffer 失败: " << ff_err2str(ret) << std::endl;
        return false;
    }

    // SDL 窗口/渲染器/纹理
    SDL_CreateWindowAndRenderer(
        g_player.video_ctx->width, g_player.video_ctx->height,
        SDL_WINDOW_RESIZABLE,
        &g_player.window, &g_player.renderer
    );
    if (!g_player.window || !g_player.renderer) {
        std::cerr << "视频: SDL_CreateWindowAndRenderer 失败: " << SDL_GetError() << std::endl;
        return false;
    }
    g_player.texture = SDL_CreateTexture(
        g_player.renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        g_player.video_ctx->width, g_player.video_ctx->height
    );
    if (!g_player.texture) {
        std::cerr << "视频: SDL_CreateTexture 失败: " << SDL_GetError() << std::endl;
        return false;
    }
    return true;
}

// -------------------------- 解封装线程（读取包并分发） --------------------------
void demux_thread() {
    AVRational atb = g_player.fmt_ctx->streams[g_player.audio_stream_idx]->time_base;
    AVPacketPtr pkt(av_packet_alloc());
    AVFramePtr aframe(av_frame_alloc());
    AVFramePtr vframe(av_frame_alloc());

    while (!g_player.quit.load(std::memory_order_relaxed) &&
           av_read_frame(g_player.fmt_ctx.get(), pkt.get()) >= 0) {
        if (pkt->stream_index == g_player.video_stream_idx) {
            if (avcodec_send_packet(g_player.video_ctx.get(), pkt.get()) == 0) {
                while (avcodec_receive_frame(g_player.video_ctx.get(), vframe.get()) == 0) {
                    AVFramePtr out(av_frame_clone(vframe.get()));
                    if (out) g_player.video_queue.push(std::move(out));
                    av_frame_unref(vframe.get());
                }
            }
        } else if (pkt->stream_index == g_player.audio_stream_idx) {
            if (avcodec_send_packet(g_player.audio_ctx.get(), pkt.get()) == 0) {
                while (avcodec_receive_frame(g_player.audio_ctx.get(), aframe.get()) == 0) {
                    int64_t ts = aframe->best_effort_timestamp;
                    double pts_sec = ts_to_sec(ts, atb);
                    if (!std::isnan(pts_sec)) {
                        std::lock_guard<std::mutex> lock(g_player.audio_clock_mtx);
                        if (!g_player.audio_start_pts_valid) {
                            g_player.audio_start_pts_valid = true;
                            g_player.audio_start_pts_sec = pts_sec;
                            g_player.audio_played_samples = 0;
                            g_player.audio_clock.store(pts_sec, std::memory_order_relaxed);
                        }
                    }

                    int64_t delay = swr_get_delay(g_player.swr_ctx.get(), g_player.audio_ctx->sample_rate);
                    int dst_nb_samples = (int)av_rescale_rnd(delay + aframe->nb_samples,
                                                            g_player.out_sample_rate,
                                                            g_player.audio_ctx->sample_rate,
                                                            AV_ROUND_UP);

                    uint8_t* out_buf = nullptr;
                    int out_linesize = 0;
                    if (av_samples_alloc(&out_buf, &out_linesize,
                                         g_player.out_channels, dst_nb_samples,
                                         g_player.out_sample_fmt, 0) < 0) {
                        av_frame_unref(aframe.get());
                        break;
                    }

                    uint8_t* out_data[1] = { out_buf };
                    int converted = swr_convert(g_player.swr_ctx.get(),
                                                out_data, dst_nb_samples,
                                                (const uint8_t**)aframe->data, aframe->nb_samples);
                    if (converted > 0) {
                        std::lock_guard<std::mutex> lock(g_player.audio_fifo_mtx);
                        if (g_player.audio_fifo) {
                            if (av_audio_fifo_realloc(g_player.audio_fifo,
                                                      av_audio_fifo_size(g_player.audio_fifo) + converted) >= 0) {
                                void* in_data[1] = { out_buf };
                                av_audio_fifo_write(g_player.audio_fifo, in_data, converted);
                            }
                        }
                    }
                    av_freep(&out_buf);
                    av_frame_unref(aframe.get());
                }
            }
        }
        av_packet_unref(pkt.get());
    }

    // flush
    avcodec_send_packet(g_player.video_ctx.get(), nullptr);
    while (avcodec_receive_frame(g_player.video_ctx.get(), vframe.get()) == 0) {
        AVFramePtr out(av_frame_clone(vframe.get()));
        if (out) g_player.video_queue.push(std::move(out));
        av_frame_unref(vframe.get());
    }
}

// -------------------------- 主函数 --------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "用法: ./av_player test.mp4" << std::endl;
        return -1;
    }

    // 初始化 FFmpeg
    avformat_network_init();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL_Init 失败: " << SDL_GetError() << std::endl;
        return -1;
    }

    // 打开视频文件
    AVFormatContext* fmt_ctx = nullptr;
    int ret = avformat_open_input(&fmt_ctx, argv[1], nullptr, nullptr);
    if (ret < 0) {
        std::cerr << "打开文件失败: " << ff_err2str(ret) << std::endl;
        return -1;
    }
    g_player.fmt_ctx = AVFormatContextPtr(fmt_ctx);
    ret = avformat_find_stream_info(g_player.fmt_ctx.get(), nullptr);
    if (ret < 0) {
        std::cerr << "读取流信息失败: " << ff_err2str(ret) << std::endl;
        return -1;
    }

    // 查找音视频流
    for (unsigned int i = 0; i < g_player.fmt_ctx->nb_streams; i++) {
        AVCodecParameters* par = g_player.fmt_ctx->streams[i]->codecpar;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO && g_player.video_stream_idx < 0) {
            g_player.video_stream_idx = i;
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO && g_player.audio_stream_idx < 0) {
            g_player.audio_stream_idx = i;
        }
    }

    if (g_player.video_stream_idx < 0 || g_player.audio_stream_idx < 0) {
        std::cerr << "未找到音视频流" << std::endl;
        return -1;
    }

    // 初始化音视频
    if (!init_audio() || !init_video()) {
        std::cerr << "初始化失败" << std::endl;
        return -1;
    }

    // 启动线程（只保留解封装/解码线程；SDL 渲染放到主线程）
    std::thread demux_t(demux_thread);

    // SDL 事件循环
    SDL_Event event;
    AVRational vtb = g_player.fmt_ctx->streams[g_player.video_stream_idx]->time_base;
    AVFramePtr pending_frame;
    while (!g_player.quit.load(std::memory_order_relaxed)) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                g_player.quit.store(true, std::memory_order_relaxed);
            }
        }

        if (!pending_frame) {
            g_player.video_queue.try_pop(pending_frame);
        }

        if (pending_frame) {
            // 音视频同步：以音频为基准（不在主线程长时间 sleep，避免拖拽缩放时“卡死”）
            int64_t ts = pending_frame->best_effort_timestamp;
            double pts = ts_to_sec(ts, vtb);
            if (std::isnan(pts)) pts = g_player.video_clock;
            double diff = pts - g_player.audio_clock.load(std::memory_order_relaxed);

            if (diff > SYNC_THRESHOLD) {
                SDL_Delay(1);
                continue;
            }
            if (diff < -SYNC_THRESHOLD * 2) {
                pending_frame.reset(); // 丢帧
                continue;
            }

            // 转换为 RGB24
            sws_scale(
                g_player.sws_ctx.get(),
                pending_frame->data, pending_frame->linesize,
                0, g_player.video_ctx->height,
                g_player.rgb_frame->data, g_player.rgb_frame->linesize
            );

            // SDL 渲染（主线程）
            SDL_UpdateTexture(g_player.texture, nullptr, g_player.rgb_frame->data[0], g_player.rgb_frame->linesize[0]);
            SDL_RenderClear(g_player.renderer);
            SDL_RenderCopy(g_player.renderer, g_player.texture, nullptr, nullptr);
            SDL_RenderPresent(g_player.renderer);

            g_player.video_clock = pts;
            pending_frame.reset();
        } else {
            SDL_Delay(1);
        }
    }

    // 等待线程退出
    g_player.video_queue.setAbort();
    demux_t.join();

    // 释放资源
    SDL_CloseAudioDevice(g_player.audio_dev);
    SDL_DestroyTexture(g_player.texture);
    SDL_DestroyRenderer(g_player.renderer);
    SDL_DestroyWindow(g_player.window);
    {
        std::lock_guard<std::mutex> lock(g_player.audio_fifo_mtx);
        if (g_player.audio_fifo) {
            av_audio_fifo_free(g_player.audio_fifo);
            g_player.audio_fifo = nullptr;
        }
    }
    SDL_Quit();

    std::cout << "播放结束" << std::endl;
    return 0;
}