# 音视频基础理论与 FFmpeg API 知识库

本文档用于整理音视频开发入门阶段最常见的基础概念，并结合 `Demos/EasyPlayer` 项目说明 FFmpeg 中已经使用到的核心 API，以及后续继续扩展播放器、转码器、录制器时会接触到但当前项目暂未使用的核心 API。

## 1. 音频基础理论

音频的本质是连续变化的声波。计算机无法直接保存连续信号，所以需要把声音离散化，变成一串数字样本。这个过程通常包括采样、量化和编码。

### 1.1 采样率

采样率表示每秒采集多少次声音样本，单位是 Hz。

常见采样率：

- 8000 Hz：电话语音常见采样率。
- 16000 Hz：语音识别、语音通话常见采样率。
- 44100 Hz：CD 音频常见采样率。
- 48000 Hz：视频、直播、专业音视频系统中常见采样率。

采样率越高，理论上能表示的声音频率范围越宽，但数据量也越大。根据奈奎斯特采样定理，采样率至少要达到最高声音频率的 2 倍，才能较完整地还原声音。

例如 44100 Hz 的采样率理论上可以表示最高约 22050 Hz 的声音频率，这已经覆盖大多数人耳可听范围。

### 1.2 位深

位深表示每个采样点用多少 bit 表示，也叫采样精度。

常见位深：

- 8 bit：每个采样点有 256 个取值。
- 16 bit：每个采样点有 65536 个取值，PCM S16 是很常见的格式。
- 24 bit：专业录音和高质量音频处理中常见。
- 32 bit float：音频编辑、混音、DSP 处理中常见。

位深越高，声音动态范围越大，量化噪声越小，但数据量也越大。

数据量估算公式：

```text
音频裸数据码率 = 采样率 * 位深 * 声道数
```

例如 48000 Hz、16 bit、双声道 PCM：

```text
48000 * 16 * 2 = 1536000 bit/s ≈ 1.536 Mbps
```

### 1.3 声道数

声道数表示同时保存多少路音频信号。

常见声道：

- 单声道：1 路声音，常见于语音场景。
- 双声道：左声道和右声道，常见于音乐和视频。
- 5.1 声道：家庭影院常见环绕声格式。
- 7.1 声道：更复杂的环绕声系统。

声道数越多，空间感越强，但数据量也越大。`EasyPlayer` 中为了简化播放流程，统一将音频输出为双声道 S16 格式。

## 2. PCM 原始音频格式

PCM 是 Pulse Code Modulation 的缩写，即脉冲编码调制。它是未压缩的原始音频数据格式，可以理解为“采样之后直接保存下来的音频样本”。

PCM 数据本身通常不包含采样率、声道数、位深等元信息，所以单独播放 `.pcm` 文件时必须额外指定这些参数。

常见 PCM 相关概念：

- S16：每个采样点是 16 bit 有符号整数。
- S16LE：16 bit 有符号整数，小端序。
- FLTP：float planar，每个声道的数据分开存放，AAC 解码后常见这种格式。
- packed：多个声道样本交错存放，例如 LRLRLR。
- planar：每个声道单独连续存放，例如 LLL... 和 RRR... 分开。

双声道 packed PCM S16 的内存排列大致如下：

```text
L0 R0 L1 R1 L2 R2 L3 R3 ...
```

双声道 planar PCM 的内存排列大致如下：

```text
L0 L1 L2 L3 ...
R0 R1 R2 R3 ...
```

`EasyPlayer` 中使用 `SwrContext` 将解码后的音频转换为 SDL2 更容易播放的 `AV_SAMPLE_FMT_S16`，再写入 `AVAudioFifo`，最后由 SDL2 音频回调拉取播放。

## 3. 视频基础理论

视频可以理解为连续显示的图像序列。每一张图像叫一帧，连续快速播放多帧图像，人眼就会感受到运动。

### 3.1 分辨率

分辨率表示一帧图像的宽和高，单位是像素。

常见分辨率：

- 1280x720：720p
- 1920x1080：1080p
- 2560x1440：2K
- 3840x2160：4K

分辨率越高，画面越清晰，但每帧数据量越大，解码、渲染、传输和存储压力也越大。

一帧 RGB24 图像的裸数据大小可以这样估算：

```text
宽 * 高 * 3 字节
```

例如 1920x1080 RGB24：

```text
1920 * 1080 * 3 ≈ 5.93 MB/帧
```

如果是 30 fps，未经压缩的数据量会非常大，所以实际视频通常需要编码压缩。

### 3.2 帧率

帧率表示每秒显示多少帧，单位是 fps。

常见帧率：

- 24 fps：电影常见帧率。
- 25 fps：PAL 制式和部分电视系统常见。
- 30 fps：网络视频常见。
- 60 fps：游戏、体育、直播中常见。

帧率越高，运动越流畅，但需要更高的解码和渲染性能，也会增加码率需求。

### 3.3 码率

码率表示单位时间内传输或存储的数据量，常用单位是 bit/s、Kbps、Mbps。

码率越高，通常画质或音质越好，但文件体积和网络带宽要求也越高。码率不是越高越好，超过一定范围后肉眼感知提升会变小，但带宽和存储成本会继续增加。

常见码率控制方式：

- CBR：恒定码率，码率稳定，适合直播推流等带宽敏感场景。
- VBR：可变码率，复杂画面分配更多码率，简单画面分配更少码率，适合文件存储。
- CRF：以恒定质量为目标，H.264/H.265 编码中常见。

### 3.4 I/P/B 帧

现代视频编码并不是每一帧都完整保存，而是利用帧与帧之间的相似性进行压缩。

I 帧：

- Intra Frame，帧内编码帧。
- 可以独立解码，不依赖其他帧。
- 文件体积较大。
- 常作为随机访问、seek、播放起点的关键帧。

P 帧：

- Predictive Frame，前向预测帧。
- 依赖前面的 I 帧或 P 帧进行解码。
- 文件体积比 I 帧小。

B 帧：

- Bi-directional Frame，双向预测帧。
- 可以同时参考前后帧。
- 压缩率通常更高。
- 会引入显示顺序和解码顺序不一致的问题。

由于 B 帧存在，视频中经常会出现 DTS 和 PTS 不同的情况：

- DTS：Decode Timestamp，解码时间戳。
- PTS：Presentation Timestamp，显示时间戳。

播放器渲染视频时通常更关心 PTS，因为它决定帧应该在什么时候显示。`EasyPlayer` 中使用 `best_effort_timestamp` 计算视频帧显示时间。

## 4. YUV 与 RGB 色彩空间

### 4.1 RGB

RGB 使用红、绿、蓝三个颜色分量表示图像，是显示器和图形 API 中非常常见的格式。

RGB24 表示每个像素使用 3 个字节：

```text
R G B
```

RGB 格式适合显示，但不一定适合视频压缩。因为人眼对亮度更敏感，对色度变化相对不敏感，视频编码通常更喜欢使用 YUV。

### 4.2 YUV

YUV 将图像信息拆成亮度和色度：

- Y：亮度，也就是黑白信息。
- U：蓝色色度差。
- V：红色色度差。

YUV 的优势是可以对色度分量进行降采样，在人眼不明显感知画质下降的情况下减少数据量。

常见 YUV 采样格式：

- YUV444：Y、U、V 三个分量分辨率相同。
- YUV422：水平方向色度减半。
- YUV420：水平方向和垂直方向的色度都减半。

### 4.3 YUV420P

YUV420P 是视频编码中非常常见的像素格式。这里的 `P` 表示 planar，也就是平面格式，Y、U、V 三个分量分开连续存放。

对于一张宽 `W`、高 `H` 的 YUV420P 图像：

- Y 平面大小是 `W * H`。
- U 平面大小是 `W/2 * H/2`。
- V 平面大小是 `W/2 * H/2`。

总大小为：

```text
W * H + W * H / 4 + W * H / 4 = W * H * 1.5
```

例如 1920x1080 的一帧 YUV420P：

```text
1920 * 1080 * 1.5 ≈ 2.97 MB/帧
```

这比 RGB24 的约 5.93 MB/帧小很多。

YUV420P 内存排列大致如下：

```text
YYYYYYYYYYYY...
UUUUUU...
VVVVVV...
```

需要注意，YUV420P 不是压缩格式，它仍然是原始像素格式，只是通过色度降采样减少了数据量。

### 4.4 YUV 与 RGB 转换

视频解码器输出的常见格式可能是 YUV420P、NV12、YUVJ420P 等，而 SDL2 的纹理或显示流程可能更适合 RGB24、RGBA 或 YUV 纹理。

`EasyPlayer` 中使用 FFmpeg 的 `libswscale` 完成像素格式转换：

```text
输入：解码后的原始视频帧，例如 YUV420P
输出：RGB24
```

核心 API 是：

- `sws_getContext`：创建像素格式转换上下文。
- `sws_scale`：执行像素格式转换和缩放。
- `sws_freeContext`：释放转换上下文。

## 5. 封装格式与编码格式

封装格式和编码格式是音视频开发中非常容易混淆的两个概念。

封装格式解决的是“如何把音频、视频、字幕、元数据等多路数据组织在一个文件或流里”的问题。

编码格式解决的是“音频或视频本身如何压缩”的问题。

例如一个 `.mp4` 文件里可能包含：

- 视频编码：H.264
- 音频编码：AAC
- 字幕流：mov_text
- 元数据：时长、标题、旋转角度等

### 5.1 常见封装格式

MP4：

- 常见文件后缀是 `.mp4`。
- 适合点播、移动端、网页播放。
- 常搭配 H.264/H.265 + AAC。
- 对文件头和索引信息依赖较强。

FLV：

- 常见文件后缀是 `.flv`。
- 曾经广泛用于直播和网页视频。
- 结构相对简单，适合流式传输。
- 现代场景中逐渐被 HLS、DASH、fMP4 等替代。

MKV：

- 常见文件后缀是 `.mkv`。
- 容器能力很强，支持多音轨、多字幕、多章节。
- 常用于高清影视文件。

TS：

- 常见文件后缀是 `.ts`。
- MPEG-TS 适合广播电视、直播和 HLS 分片。
- 容错能力较强，适合网络传输。

### 5.2 常见编码格式

H.264：

- 非常常见的视频编码格式。
- 压缩效率较高，兼容性很好。
- 广泛用于 MP4、FLV、TS、MKV 等封装中。

H.265/HEVC：

- 比 H.264 压缩效率更高。
- 同等画质下码率可以更低。
- 编码复杂度更高，兼容性和授权问题需要注意。

AAC：

- 常见音频编码格式。
- 常用于 MP4、直播、短视频等场景。
- 比 MP3 在中低码率下通常有更好表现。

MP3：

- 兼容性极好。
- 主要用于音乐和简单音频播放场景。

### 5.3 一句话区分

可以把封装格式理解为“箱子”，把编码格式理解为“箱子里压缩后的货物”。

```text
MP4 / FLV / MKV / TS = 容器
H.264 / H.265 / AAC / MP3 = 编码方式
```

同一种封装格式可以装不同编码格式，同一种编码格式也可以放进不同封装格式中。

## 6. EasyPlayer 中涉及到的 FFmpeg 模块

`EasyPlayer` 当前主要使用了以下 FFmpeg 模块：

- `libavformat`：负责打开媒体文件、读取封装信息、读取音视频 packet。
- `libavcodec`：负责查找解码器、创建解码上下文、音视频解码。
- `libswresample`：负责音频重采样和采样格式转换。
- `libswscale`：负责视频像素格式转换。
- `libavutil`：提供基础数据结构、时间基转换、音频 FIFO、采样格式、声道布局等工具能力。

## 7. EasyPlayer 中已经使用的 FFmpeg 核心 API

### 7.1 初始化与输入文件

`avformat_network_init`

- 初始化 FFmpeg 网络模块。
- 对本地文件播放不是绝对必需，但如果后续支持 RTSP、HTTP、HLS 等网络流会用到。

`avformat_open_input`

- 打开输入媒体文件或输入流。
- 创建并初始化 `AVFormatContext`。

`avformat_find_stream_info`

- 读取更多包来分析媒体流信息。
- 用于获取时长、编码参数、时间基、音视频流信息等。

`avformat_close_input`

- 关闭输入文件并释放 `AVFormatContext`。
- `EasyPlayer` 中通过智能指针删除器间接调用。

### 7.2 流与编码参数

`AVFormatContext`

- 表示一个输入或输出媒体容器。
- 包含所有 stream、封装格式信息、时长、元数据等。

`AVStream`

- 表示容器中的一路媒体流，例如一路视频、一路音频或一路字幕。
- `EasyPlayer` 通过遍历 `fmt_ctx->streams` 查找音频流和视频流。

`AVCodecParameters`

- 保存编码参数，例如 codec id、宽高、采样率、声道布局等。
- `EasyPlayer` 使用它来创建和初始化 `AVCodecContext`。

### 7.3 解码器

`avcodec_find_decoder`

- 根据 `codec_id` 查找对应解码器。
- 例如 H.264 视频流会找到 H.264 解码器，AAC 音频流会找到 AAC 解码器。

`avcodec_alloc_context3`

- 为解码器分配 `AVCodecContext`。

`avcodec_parameters_to_context`

- 将 `AVCodecParameters` 中的参数复制到 `AVCodecContext`。

`avcodec_open2`

- 打开解码器，使 `AVCodecContext` 进入可解码状态。

`avcodec_send_packet`

- 将压缩后的 `AVPacket` 送入解码器。

`avcodec_receive_frame`

- 从解码器取出解码后的 `AVFrame`。

`avcodec_free_context`

- 释放 `AVCodecContext`。
- `EasyPlayer` 中通过智能指针删除器间接调用。

### 7.4 Packet 与 Frame

`AVPacket`

- 表示压缩后的音视频数据包。
- 从封装层读取出来，送给解码器。

`AVFrame`

- 表示解码后的原始音频帧或视频帧。
- 音频帧通常包含 PCM 数据。
- 视频帧通常包含 YUV 或 RGB 像素数据。

`av_packet_alloc`

- 分配 `AVPacket`。

`av_packet_unref`

- 释放 packet 内部引用的数据，让 packet 可以复用。

`av_packet_free`

- 释放 `AVPacket` 本身。

`av_frame_alloc`

- 分配 `AVFrame`。

`av_frame_clone`

- 克隆一个 frame 引用。
- `EasyPlayer` 中用于把解码出的 `vframe` 放入视频队列。

`av_frame_unref`

- 释放 frame 内部引用的数据，让 frame 可以复用。

`av_frame_free`

- 释放 `AVFrame` 本身。

`av_frame_get_buffer`

- 为 frame 分配实际数据缓冲区。
- `EasyPlayer` 中用于给 RGB 输出帧分配缓冲。

### 7.5 读取媒体包

`av_read_frame`

- 从输入媒体中读取下一个 `AVPacket`。
- 读取出的 packet 可能属于音频流、视频流、字幕流或其他流。

`EasyPlayer` 根据 `pkt->stream_index` 判断 packet 属于音频还是视频，然后送入对应解码器。

### 7.6 时间戳与时间基

`AVRational`

- FFmpeg 中常用的有理数结构。
- stream 的 `time_base` 就是 `AVRational`。

`av_q2d`

- 将 `AVRational` 转为 `double`。
- `EasyPlayer` 用它将时间戳转换为秒。

`AV_NOPTS_VALUE`

- 表示没有有效时间戳。

`best_effort_timestamp`

- FFmpeg 尽量推导出的帧显示时间戳。
- 对有 B 帧的视频很有用。

`av_rescale_rnd`

- 按比例换算整数并支持指定取整方式。
- `EasyPlayer` 中用于计算重采样后的目标采样数量。

### 7.7 音频重采样

`AVChannelLayout`

- FFmpeg 新版本中表示声道布局的结构。
- 比旧版 `channel_layout` 更清晰。

`av_channel_layout_default`

- 根据声道数生成默认声道布局。

`av_channel_layout_uninit`

- 释放声道布局内部资源。

`swr_alloc_set_opts2`

- 创建并配置 `SwrContext`。
- `EasyPlayer` 用它将输入音频转换为双声道 S16。

`swr_init`

- 初始化重采样上下文。

`swr_get_delay`

- 获取重采样器内部延迟，用于计算输出采样数量。

`swr_convert`

- 执行音频重采样、采样格式转换、声道布局转换。

`swr_free`

- 释放 `SwrContext`。

### 7.8 音频 FIFO 与样本工具

`av_audio_fifo_alloc`

- 创建音频 FIFO。
- 适合在解码线程和音频播放回调之间缓存 PCM 数据。

`av_audio_fifo_size`

- 查询 FIFO 中已有多少个采样点。

`av_audio_fifo_realloc`

- 调整 FIFO 容量。

`av_audio_fifo_write`

- 向 FIFO 写入音频样本。

`av_audio_fifo_read`

- 从 FIFO 读取音频样本。

`av_audio_fifo_free`

- 释放音频 FIFO。

`av_get_bytes_per_sample`

- 获取某种采样格式下每个采样点占用的字节数。

`av_samples_alloc`

- 为音频样本分配缓冲区。

`av_freep`

- 释放指针并将指针置空。

### 7.9 视频像素格式转换

`sws_getContext`

- 创建图像转换上下文。
- 可用于像素格式转换和缩放。

`sws_scale`

- 执行图像转换。
- `EasyPlayer` 中用于将解码后的视频帧转换为 RGB24。

`sws_freeContext`

- 释放 `SwsContext`。

## 8. EasyPlayer 当前没有涉及到的 FFmpeg 核心 API

下面这些 API 在音视频开发中也很常见，但当前 `EasyPlayer` 还没有使用。它们通常会出现在转码、录制、推流、滤镜、硬件加速、seek、截图等更复杂的场景中。

### 8.1 输出文件、封装与转码

`avformat_alloc_output_context2`

- 创建输出封装上下文。
- 用于生成 MP4、FLV、MKV、TS 等输出文件或输出流。

`avformat_new_stream`

- 在输出容器中创建一路新的音频流或视频流。

`avio_open`

- 打开输出文件或网络输出地址。

`avformat_write_header`

- 写入输出文件头。

`av_write_frame`

- 写入一个 packet。
- 不一定保证音视频交错顺序合理。

`av_interleaved_write_frame`

- 写入一个 packet，并由 FFmpeg 帮助处理音视频交错。
- 实际写文件或推流时更常用。

`av_write_trailer`

- 写入文件尾，完成封装。

### 8.2 编码

`avcodec_find_encoder`

- 查找编码器，例如 H.264、AAC 编码器。

`avcodec_send_frame`

- 将原始音频帧或视频帧送入编码器。

`avcodec_receive_packet`

- 从编码器取出压缩后的 `AVPacket`。

这些 API 和解码 API 的方向相反。解码是 `AVPacket -> AVFrame`，编码是 `AVFrame -> AVPacket`。

### 8.3 seek 与播放控制

`av_seek_frame`

- 按时间戳跳转到指定位置附近。
- 常用于播放器进度条拖动。

`avformat_seek_file`

- 更灵活的 seek API，可以指定最小、目标、最大时间戳。

`avcodec_flush_buffers`

- seek 后清空解码器内部缓存，避免旧帧影响新位置播放。

如果 `EasyPlayer` 后续要增加进度条拖动功能，就需要引入 seek API，并处理队列清空、音频时钟重置、解码器 flush 等逻辑。

### 8.4 滤镜

`avfilter_graph_alloc`

- 创建滤镜图。

`avfilter_graph_create_filter`

- 创建滤镜节点，例如 buffer、buffersink、scale、volume 等。

`avfilter_graph_parse_ptr`

- 根据滤镜字符串解析滤镜图。

`avfilter_graph_config`

- 配置滤镜图。

`av_buffersrc_add_frame_flags`

- 向滤镜输入端送入 frame。

`av_buffersink_get_frame`

- 从滤镜输出端取出处理后的 frame。

滤镜可以实现缩放、裁剪、旋转、水印、音量调整、混音、变速等功能。

### 8.5 Bitstream Filter

`av_bsf_get_by_name`

- 按名称查找 bitstream filter。

`av_bsf_alloc`

- 创建 bitstream filter 上下文。

`avcodec_parameters_copy`

- 复制 codec 参数到 bitstream filter。

`av_bsf_send_packet`

- 向 bitstream filter 输入 packet。

`av_bsf_receive_packet`

- 从 bitstream filter 输出 packet。

常见用途是处理 H.264/H.265 码流格式转换，例如 `h264_mp4toannexb`，在 MP4 转 TS、推流等场景中经常遇到。

### 8.6 硬件加速

`av_hwdevice_ctx_create`

- 创建硬件设备上下文，例如 D3D11VA、DXVA2、CUDA、VAAPI。

`av_hwframe_transfer_data`

- 在硬件帧和系统内存帧之间传输数据。

硬件加速可以降低 CPU 解码压力，但会增加平台相关性和数据拷贝复杂度。

### 8.7 设备采集

`avdevice_register_all`

- 旧版本中用于注册设备模块，新版本中通常不再需要显式调用。

`av_find_input_format`

- 查找输入设备格式，例如 `dshow`、`avfoundation`、`v4l2`。

使用 `libavdevice` 可以采集摄像头、麦克风、桌面等输入源。

### 8.8 图像工具

`av_image_alloc`

- 为图像分配缓冲区。

`av_image_fill_arrays`

- 根据已有内存填充 frame 的 data 和 linesize。

`av_image_get_buffer_size`

- 计算指定像素格式和分辨率所需缓冲区大小。

这些 API 在截图、手动管理图像缓冲区、像素格式转换时很常用。

### 8.9 字典与参数

`AVDictionary`

- FFmpeg 中的键值参数结构。

`av_dict_set`

- 设置参数，例如超时、协议参数、编码参数等。

`av_dict_free`

- 释放字典。

打开网络流、设置编码器参数、设置封装器参数时经常会用到 `AVDictionary`。

## 9. EasyPlayer 的技术链路总结

当前项目的核心链路可以概括为：

```text
媒体文件
  -> avformat_open_input
  -> avformat_find_stream_info
  -> av_read_frame
  -> avcodec_send_packet
  -> avcodec_receive_frame
  -> 音频：swr_convert -> AVAudioFifo -> SDL2 音频回调
  -> 视频：视频帧队列 -> sws_scale -> SDL2 纹理渲染
```

从学习角度看，`EasyPlayer` 已经覆盖了播放器最关键的基础能力：

- 读封装。
- 找音视频流。
- 解码音视频。
- 重采样音频。
- 转换视频像素格式。
- 建立音频时钟。
- 基于音频时钟做视频同步。
- 将 FFmpeg 解码结果交给 SDL2 播放和显示。

后续如果继续扩展，可以优先考虑下面几个方向：

- 增加 `.gitignore` 并清理构建产物。
- 支持 seek 和暂停继续。
- 支持只有音频或只有视频的文件。
- 使用 SDL2 的 YUV 纹理，减少 YUV 到 RGB 的转换成本。
- 引入 `libavfilter` 做音量、缩放、旋转、变速等处理。
- 增加编码与封装能力，扩展成转码 Demo。
- 增加硬件解码支持，降低高分辨率视频播放时的 CPU 压力。

## 10. 建议学习顺序

如果从零学习音视频开发，可以按下面顺序推进：

1. 理解 PCM、采样率、位深、声道数。
2. 理解 RGB、YUV、YUV420P 和像素格式转换。
3. 理解封装格式和编码格式的区别。
4. 学习 FFmpeg 的 `AVFormatContext`、`AVStream`、`AVPacket`、`AVFrame`。
5. 学习解码流程：`av_read_frame`、`avcodec_send_packet`、`avcodec_receive_frame`。
6. 学习音频重采样：`SwrContext` 和 `swr_convert`。
7. 学习视频转换：`SwsContext` 和 `sws_scale`。
8. 学习时间戳、time_base、PTS、DTS 和音视频同步。
9. 学习 seek、滤镜、编码、封装和推流等进阶主题。

掌握这些内容后，就可以从简单播放器继续扩展到转码器、录屏工具、直播推流器、视频处理工具等更完整的音视频应用。