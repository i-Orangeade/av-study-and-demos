# av-study-and-demos

这是一个面向音视频开发、流媒体协议和实时通信的学习仓库。仓库以一组尽量小、可运行、便于阅读的 Demo 为主线，配合知识笔记整理从原始音视频数据、FFmpeg API、播放器、转码器，到 TCP/UDP、HTTP、FLV、RTMP、RTSP、RTP、HLS 和 WebRTC 的核心概念。

本仓库不是生产级音视频框架，而是用于拆解音视频工程中的关键链路：每个 Demo 聚焦一个相对独立的问题，尽量少引入第三方封装，让代码和协议细节直接对应。

## 仓库内容

```text
av-study-and-demos/
├── AV-Demos/          # 音视频基础、FFmpeg、SDL2、播放器和转码 Demo
├── Network-Demos/     # 网络编程与流媒体协议 Demo
├── WebRTC-Demos/      # WebRTC 信令、P2P 通话和 C++ Native 客户端学习入口
├── Notes/             # 音视频、流媒体协议和 WebRTC 知识笔记
└── .gitignore
```

## AV-Demos

`AV-Demos` 关注音视频文件处理、原始帧显示、解封装、解码、播放和转码。

| Demo | 说明 |
| --- | --- |
| `AV-YUV420P-Player` | 使用 C++17 + SDL2 播放 YUV420P 裸视频，理解原始视频帧布局和 SDL2 YUV 纹理渲染。 |
| `AV-Demux-Extractor` | 使用 FFmpeg 解封装 MP4，提取 H.264 Annex-B 裸流和带 ADTS 头的 AAC 裸流。 |
| `AV-Easy-Player` | 使用 FFmpeg + SDL2 实现简易播放器，覆盖解封装、解码、音频播放、视频渲染和基础音视频同步。 |
| `AV-Transcoder` | 使用 FFmpeg 实现简易转码器，完成解封装、解码、像素格式转换、音频重采样、H.264/AAC 编码和 MP4 封装。 |

## Network-Demos

`Network-Demos` 关注 Linux 网络编程基础和常见流媒体协议的手写解析。

| Demo | 说明 |
| --- | --- |
| `AV-Net-TCP-Echo` | 基于 POSIX Socket 和 epoll 的 TCP Echo 服务端/客户端，用于理解非阻塞 IO 和多客户端连接管理。 |
| `HTTP-File-Downloader` | 手写 HTTP/1.1 文件下载器，支持基础响应解析、文件写入和 Range 断点续传。 |
| `UDP-Broadcast` | UDP 广播发送/接收 Demo，用于理解无连接通信、广播地址和抓包验证。 |
| `FLV-Parser` | 手写 FLV 文件解析器，解析 FLV Header/Tag，并导出 H.264/AAC 裸流。 |
| `RTMP-Handshake` | 手写 RTMP simple handshake、AMF0 connect/createStream 和基础 Chunk Header 观察。 |
| `RTMP-FLV-Recorder` | RTMP 拉流录制骨架，展示从 RTMP 连接、拉流命令到 FLV 文件写入的关键步骤。 |
| `HTTP-FLV-Player` | HTTP-FLV 拉流与 FLV Tag 实时解析骨架，后续可接入 FFmpeg 解码和 SDL2 播放。 |
| `M3U8-Parser` | HLS M3U8 解析 Demo，支持本地文件和 HTTP playlist，解析主 playlist、媒体 playlist 和切片 URI。 |
| `HLS-Player` | HLS 播放器调度层骨架，展示 playlist 下载、切片 URL 拼接和预加载思路。 |
| `RTSP-Client` | 手写 RTSP 控制链路，覆盖 OPTIONS、DESCRIBE、SETUP、PLAY、TEARDOWN 和 SDP 解析。 |
| `RTP-Packet-Parser` | RTP/H.264 包解析 Demo，支持 Single NALU 和 FU-A 分片重组，输出 Annex-B H.264。 |

## WebRTC-Demos

`WebRTC-Demos` 主要用于记录 WebRTC 学习路线和少量配套实验。WebRTC 本身工程复杂度很高，相关知识和 Demo 更推荐优先阅读官方资料、浏览器示例和成熟开源仓库，本目录中的内容更适合作为学习过程中的补充笔记和验证入口。

| Demo | 说明 |
| --- | --- |
| `P2P-Signaling-Server` | 使用 Node.js + `ws` 实现最小 WebSocket 信令服务器，转发 join、leave、offer、answer 和 candidate。 |
| `Cpp-WebRTC-Client` | C++ WebRTC Native 客户端学习入口，记录信令接入、PeerConnection、STUN/TURN、自定义音视频源和渲染路线。 |

推荐参考：

- [WebRTC 官方网站](https://webrtc.org/)：理解 WebRTC 能力、架构和 Native 开发入口。
- [MDN WebRTC API](https://developer.mozilla.org/en-US/docs/Web/API/WebRTC_API)：学习浏览器侧 PeerConnection、MediaStream、DataChannel 等 API。
- [WebRTC 官方 Samples](https://webrtc.github.io/samples/) / [源码仓库](https://github.com/webrtc/samples)：适合通过浏览器 Demo 学习 Offer/Answer、ICE、媒体采集和 DataChannel。
- [WebRTC Native 源码](https://webrtc.googlesource.com/src)：深入 C++ Native 实现、接口和示例工程。
- [Pion WebRTC](https://github.com/pion/webrtc)：Go 语言 WebRTC 实现，适合学习协议层和服务端实时通信。
- [aiortc](https://github.com/aiortc/aiortc)：Python WebRTC 实现，适合快速实验信令、媒体轨道和数据通道。
- [Janus Gateway](https://github.com/meetecho/janus-gateway)、[mediasoup](https://github.com/versatica/mediasoup)、[LiveKit](https://github.com/livekit/livekit)：适合学习 SFU、会议、直播连麦和生产级实时音视频服务端设计。

## Notes

`Notes` 中整理了与 Demo 对应的知识笔记：

- `av-foundation-and-ffmpeg-api.md`：音视频基础概念、FFmpeg API、播放器和转码链路。
- `streaming-protocols-and-networking.md`：TCP/UDP、HTTP、FLV、RTMP、RTSP、RTP、HLS 等网络与流媒体知识。
- `webrtc-core-and-realtime-communication.md`：WebRTC 架构、PeerConnection、SDP、ICE、RTP/RTCP、STUN/TURN 和实时通信知识。

## 推荐学习顺序

1. 从 `AV-YUV420P-Player` 理解原始视频帧和 SDL2 渲染。
2. 学习 `AV-Demux-Extractor`，区分容器格式、编码格式、`AVPacket` 和 `AVFrame`。
3. 阅读并运行 `AV-Easy-Player`，串起解封装、解码、音频播放、视频渲染和同步。
4. 继续学习 `AV-Transcoder`，理解编码、封装、时间基和音频 FIFO。
5. 从 `AV-Net-TCP-Echo`、`HTTP-File-Downloader`、`UDP-Broadcast` 打好网络编程基础。
6. 学习 `FLV-Parser`、`RTMP-Handshake`、`HTTP-FLV-Player`，理解直播流中的 FLV 和 RTMP/HTTP-FLV 链路。
7. 学习 `M3U8-Parser` 和 `HLS-Player`，理解 HLS playlist、切片和调度。
8. 学习 `RTSP-Client` 和 `RTP-Packet-Parser`，理解 RTSP 控制协议和 RTP 媒体传输。
9. 最后学习 WebRTC 时，优先结合官方文档、官方 Samples 和成熟开源项目，再用 `WebRTC-Demos` 记录信令、SDP/ICE 和 C++ Native 客户端实验。

## 环境依赖

主要开发和测试环境为 Ubuntu/Linux。

通用依赖：

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config
```

部分 Demo 还需要：

```bash
sudo apt install -y libsdl2-dev ffmpeg \
    libavformat-dev libavcodec-dev libavutil-dev \
    libswscale-dev libswresample-dev
```

WebRTC 信令服务器需要 Node.js 和 npm：

```bash
cd WebRTC-Demos/P2P-Signaling-Server
npm install
npm start
```

## 构建方式

每个 C++ Demo 都是相对独立的 CMake 小项目，可以进入对应目录构建：

```bash
cd AV-Demos/AV-Easy-Player
cmake -S . -B build
cmake --build build
```

也可以在仓库根目录指定子项目路径：

```bash
cmake -S Network-Demos/RTSP-Client -B Network-Demos/RTSP-Client/build
cmake --build Network-Demos/RTSP-Client/build
```

具体运行参数请查看各 Demo 目录下的 `README.md`。

## 当前定位

这个仓库更偏向“学习路线 + 可运行实验”：

- 代码优先展示核心链路，不追求完整产品化封装。
- 网络协议 Demo 尽量手写解析，便于观察协议字段和字节流。
- FFmpeg/SDL2 Demo 用于理解播放器、转码器和媒体处理工具的基础结构。
- WebRTC 部分以学习路线和实验记录为主，核心知识与完整 Demo 更推荐参考官方资料和成熟开源仓库。

后续可以继续补充更完整的协议解析、播放器控制、RTMP/RTSP/RTP 实流处理、HLS 切片下载、WebRTC Native 客户端和跨网联调案例。
