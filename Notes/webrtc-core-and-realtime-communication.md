# WebRTC 核心知识库

本文档用于整理 WebRTC 实时音视频开发需要优先掌握的知识点，并对应后续 `WebRTC-Demos` 下的自研信令服务器、跨网 P2P 穿透、C++ WebRTC 客户端、自定义音视频源和渲染等学习项目。

## 1. WebRTC 核心价值与应用场景

WebRTC，Web Real-Time Communication，是一套用于实时音视频和数据传输的技术体系。它最大的价值是把采集、编解码、网络传输、抖动缓冲、丢包恢复、拥塞控制、回声消除等复杂能力封装成可用的实时通信栈。

核心价值：

- 低延迟实时音视频，目标通常是端到端小于 300 ms。
- 跨平台支持，包括 Windows、Linux、macOS、Android、iOS 和 Web。
- 内置音视频处理能力，包括 AEC、NS、AGC、VAD、JitterBuffer、NACK、FEC、带宽估计等。
- 支持 P2P，也可以配合 SFU/MCU 构建多人会议和直播连麦系统。

典型应用场景：

- 1 对 1 音视频通话。
- 多人会议。
- 在线教育和互动课堂。
- 直播连麦。
- 远程桌面和云游戏。
- 实时数据通道，例如白板、文件传输、低延迟控制消息。

## 2. WebRTC 整体架构

WebRTC 可以按三层理解：

```text
应用层
  -> PeerConnection API / MediaStream API / DataChannel API
核心层
  -> 音视频引擎 / RTP RTCP / ICE / DTLS / SRTP / 拥塞控制
底层
  -> 操作系统接口 / 网络接口 / 摄像头 / 麦克风 / 音视频设备
```

应用层关注“如何建立连接、添加媒体、交换 SDP 和 ICE”。核心层负责“如何把音视频低延迟、安全、尽量可靠地送到对端”。底层负责与系统设备和网络栈交互。

## 3. WebRTC 核心模块

常见模块：

- 采集模块：`VideoCapture`、`AudioDevice`，负责摄像头、屏幕、麦克风等输入。
- 编码模块：`VideoEncoder`、`AudioEncoder`，常见编码包括 H.264、VP8、VP9、AV1、Opus。
- 传输模块：RTP/RTCP、ICE、DTLS、SRTP，负责媒体包发送、安全加密和网络穿透。
- 解码模块：`VideoDecoder`、`AudioDecoder`，把压缩码流还原成原始音视频帧。
- 渲染模块：`VideoRenderer`、`AudioRenderer`，负责显示视频帧和播放 PCM。
- 音频处理模块：AEC、NS、AGC、VAD，提升语音通话质量。
- 拥塞控制模块：估算带宽和网络质量，动态调整码率、帧率和分辨率。

## 4. PeerConnection 核心概念

`PeerConnection` 是 WebRTC 最核心的对象，代表一个端到端连接。应用通常不直接操作 RTP socket，而是通过 `PeerConnection` 添加媒体、创建 Offer/Answer、设置 SDP、交换 ICE Candidate。

核心概念：

- `PeerConnection`：一个端到端连接，内部管理 ICE、DTLS、SRTP、RTP/RTCP、带宽估计等。
- `MediaStream`：媒体流，包含多个 `MediaTrack`。
- `MediaTrack`：媒体轨道，分为音频轨道和视频轨道。
- `SDP`：会话描述协议，描述媒体能力、编码格式、网络地址、传输参数等。
- `ICE Candidate`：ICE 候选地址，包含 IP、端口、协议、候选类型等。
- `DataChannel`：基于 SCTP/DTLS 的实时数据通道，可用于文本、文件、控制消息。

## 5. PeerConnection 核心流程

1 对 1 通话可以按下面流程背：

```text
创建 PeerConnectionFactory
  -> 创建 PeerConnection
  -> 添加本地音频轨道和视频轨道
  -> A 创建 Offer
  -> A setLocalDescription
  -> 通过信令服务器发送 offer 给 B
  -> B setRemoteDescription
  -> B 创建 Answer
  -> B setLocalDescription
  -> 通过信令服务器发送 answer 给 A
  -> A setRemoteDescription
  -> 双方持续交换 ICE Candidate
  -> ICE 连接检查成功
  -> DTLS 握手
  -> SRTP 加密媒体传输
```

要记住：WebRTC 本身不规定信令协议。Offer、Answer、Candidate 如何传给对端，需要业务自己通过 WebSocket、HTTP、MQ 或其他方式实现。

## 6. SDP 协议基础

SDP，Session Description Protocol，用文本描述一次媒体会话。WebRTC 中 SDP 主要用于媒体能力协商和传输参数交换。

SDP 分为：

- 会话级描述：对整个会话生效。
- 媒体级描述：对某一路音频、视频或数据通道生效。

常见字段：

- `v=`：协议版本，通常是 `0`。
- `o=`：owner/creator 和 session id。
- `s=`：session name。
- `c=`：connection information，连接地址。
- `t=`：time，时间范围。
- `m=`：media description，媒体类型、端口、协议、payload type。
- `a=`：attribute，最常见，描述编码、方向、ICE、DTLS、RTP 扩展等。

简化示例：

```text
v=0
o=- 123456 2 IN IP4 127.0.0.1
s=-
t=0 0
m=audio 9 UDP/TLS/RTP/SAVPF 111
a=rtpmap:111 opus/48000/2
a=sendrecv
a=ice-ufrag:xxxx
a=ice-pwd:yyyy
a=fingerprint:sha-256 ...
m=video 9 UDP/TLS/RTP/SAVPF 96
a=rtpmap:96 H264/90000
a=sendrecv
```

媒体能力协商过程：

- Offer 端列出自己支持的 codec、RTP 扩展、传输方式和媒体方向。
- Answer 端从 Offer 中选择双方都支持的能力。
- 双方设置本地和远端 SDP 后，WebRTC 内部才能确定实际使用的编码和传输参数。

## 7. WebRTC C++ API 使用

Native C++ 开发常见接口：

- `webrtc::PeerConnectionFactoryInterface`：创建 PeerConnection、AudioTrack、VideoTrack 等对象。
- `webrtc::PeerConnectionInterface`：管理连接、SDP、ICE、媒体轨道。
- `webrtc::MediaStreamInterface`：媒体流。
- `webrtc::VideoTrackInterface`：视频轨道。
- `webrtc::AudioTrackInterface`：音频轨道。
- `webrtc::VideoTrackSourceInterface`：自定义视频源的基础接口。
- `webrtc::VideoSinkInterface<webrtc::VideoFrame>`：接收远端或本地视频帧。

核心调用顺序：

```text
CreatePeerConnectionFactory
  -> factory->CreatePeerConnection
  -> factory->CreateAudioSource / CreateAudioTrack
  -> factory->CreateVideoTrack
  -> peer_connection->AddTrack
  -> CreateOffer / CreateAnswer
  -> SetLocalDescription / SetRemoteDescription
  -> OnIceCandidate 回调中把 candidate 交给信令层
```

WebRTC Native 编译体积大、依赖多，通常需要先准备 depot_tools 和 WebRTC 源码。学习项目可以先把信令服务器、SDP/ICE 交换流程、RTP/RTCP 知识掌握清楚，再接入完整 native SDK。

## 8. 信令服务器作用

信令服务器不转发音视频媒体数据，它负责让两个客户端“找到彼此，并交换建立 WebRTC 连接所需的信息”。

主要职责：

- 交换 SDP Offer/Answer。
- 交换 ICE Candidate。
- 房间管理。
- 用户管理。
- 上下线通知。
- 异常断开清理。

常见信令类型：

- `join`：加入房间。
- `leave`：离开房间。
- `offer`：发送 SDP Offer。
- `answer`：发送 SDP Answer。
- `candidate`：发送 ICE Candidate。

基于 WebSocket 的 JSON 信令示例：

```json
{
  "type": "offer",
  "roomId": "room-001",
  "from": "alice",
  "to": "bob",
  "sdp": "v=0..."
}
```

## 9. ICE 穿透原理

ICE，Interactive Connectivity Establishment，用来在复杂网络环境下为两个端点寻找可连通路径。

候选地址类型：

- host candidate：本机局域网地址。
- srflx candidate：通过 STUN 看到的公网映射地址。
- relay candidate：通过 TURN 中继服务器分配的地址。

NAT 类型：

- 完全锥型 NAT：外部主机只要知道映射地址，通常都能发进来。
- 限制锥型 NAT：只允许本机访问过的外部 IP 发回来。
- 端口限制锥型 NAT：限制外部 IP 和端口。
- 对称型 NAT：不同目的地址会产生不同公网映射，P2P 最难穿透。

STUN 和 TURN：

- STUN：帮助客户端发现自己的公网 IP 和端口映射。
- TURN：当 P2P 失败时作为中继转发媒体，可靠但消耗服务器带宽。

ICE 流程：

```text
收集 host / srflx / relay candidates
  -> 通过信令交换 candidates
  -> 按优先级形成 candidate pair
  -> 进行连通性检查
  -> 选择可用且优先级最高的链路
  -> nominated pair 成为最终传输路径
```

跨路由器、跨局域网时，至少需要配置公网 STUN。若双方处于对称型 NAT 或企业防火墙后，通常还需要 TURN。

## 10. 自定义音视频源和渲染器

自定义视频源常用于把摄像头之外的数据送进 WebRTC，例如 FFmpeg 解码帧、屏幕采集帧、OpenGL 渲染结果。

关键接口和数据：

- `webrtc::VideoTrackSourceInterface`：自定义视频源。
- `webrtc::VideoFrame`：WebRTC 视频帧对象。
- 常见视频格式：I420、NV12、RGB。
- 视频帧需要设置时间戳，方便同步、抖动缓冲和渲染调度。

自定义视频渲染器：

- `webrtc::VideoSinkInterface<webrtc::VideoFrame>`：接收视频帧。
- SDL2 可用于快速显示 YUV/RGB。
- OpenGL 更适合高性能纹理渲染、缩放和旋转。

自定义音频源：

- `webrtc::AudioSourceInterface`：音频源抽象。
- `webrtc::AudioFrame`：音频帧。
- 常见格式：PCM。
- 关键参数：采样率、位深、声道数、每帧采样数。

自定义音频渲染器：

- `webrtc::AudioSinkInterface`：接收音频数据。
- SDL2 可用于播放 PCM。
- 音频播放要注意缓冲大小，过大增加延迟，过小容易 underrun。

## 11. RTP 协议深入

RTP 负责实时媒体包传输，WebRTC 中音视频最终都会封装为 RTP 包，并通过 SRTP 加密。

RTP Header 常见字段：

- version：版本号，通常为 2。
- padding：是否有填充字节。
- extension：是否有扩展头。
- CSRC count：CSRC 数量。
- marker：标记位，视频中常用于表示一帧结束。
- payload type：负载类型。
- sequence number：序列号，用于检测丢包和乱序。
- timestamp：媒体时间戳。
- SSRC：同步源标识。
- CSRC list：贡献源列表，常见于混音场景。

RTP 扩展头用于携带额外信息，例如音频电平、绝对发送时间、传输层序列号、视频方向等。WebRTC 的拥塞控制、同步和统计会用到部分 RTP header extension。

H.264 RTP 封装：

- 单一 NALU 模式：一个 RTP 包携带一个完整 NALU。
- FU-A 分片模式：一个大 NALU 拆成多个 RTP 包。
- STAP-A 聚合模式：一个 RTP 包聚合多个小 NALU。

Opus RTP 封装：

- payload type 常见为动态类型，例如 111。
- 时钟频率通常写作 48000。
- 一个 RTP 包可以包含一个或多个 Opus frame。
- WebRTC 音频常配合 NetEQ 做抖动缓冲、PLC 和播放节奏控制。

## 12. RTCP 协议深入

RTCP 是 RTP 的控制协议，用于质量反馈、同步和统计。

常见 RTCP 包类型：

- SR，Sender Report，发送者报告。
- RR，Receiver Report，接收者报告。
- SDES，Source Description，描述同步源。
- BYE，离开会话。
- APP，应用自定义扩展。

SR 包含：

- 发送字节数。
- 发送包数。
- NTP 时间戳。
- RTP 时间戳。

RR 包含：

- 丢包率。
- 累计丢包数。
- jitter 抖动。
- last SR 和 delay since last SR，可用于估算 RTT。

RTCP 带宽占用通常不超过总带宽的 5%。WebRTC 还会使用 NACK、PLI、FIR、REMB、Transport-CC 等反馈机制辅助丢包恢复和拥塞控制。

## 13. JitterBuffer 工作原理

抖动来自网络延迟变化。同样间隔发送的 RTP 包，可能因为网络排队、路由变化、无线干扰等原因，以不均匀间隔到达。

JitterBuffer 的作用：

- 平滑网络抖动。
- 对乱序包重新排序。
- 去重。
- 等待短时间内可能到达的迟到包。
- 丢包时触发丢包补偿或请求重传。
- 动态调整播放延迟，在低延迟和流畅度之间权衡。

WebRTC 实现：

- 音频：NetEQ，负责音频抖动缓冲、PLC、加速/减速播放等。
- 视频：VideoJitterBuffer / FrameBuffer，负责按帧组包、排序、等待参考帧、控制解码节奏。

## 14. WebRTC 拥塞控制

拥塞产生的原因是网络可用带宽不足，继续提高发送速率会导致排队延迟上升、丢包增加，最终音视频卡顿。

拥塞控制目标：

- 不把网络打爆。
- 尽量利用可用带宽。
- 在延迟、清晰度、流畅度之间动态平衡。

常见通用算法：

- TCP Reno：经典丢包驱动拥塞控制。
- TCP Cubic：Linux 常见默认 TCP 拥塞控制。
- BBR：基于瓶颈带宽和 RTT 建模。

WebRTC 常见思路：

- GCC，Google Congestion Control，主要基于延迟变化和丢包反馈估算带宽。
- BBR 思路也可用于实时传输优化，但实时音视频更关注低延迟和码率快速调整。

WebRTC 拥塞控制流程：

```text
发送端记录发送时间、包大小、序列号
  -> 接收端统计到达时间、丢包、抖动
  -> RTCP / Transport-CC 反馈网络状态
  -> 带宽估计模块计算目标码率
  -> 编码器调整码率、帧率、分辨率
```

## 15. WebRTC 音频处理模块

实时语音质量很大程度依赖音频处理。

核心模块：

- AEC，Acoustic Echo Cancellation，回声消除，避免扬声器声音再次被麦克风采集。
- NS，Noise Suppression，噪声抑制，降低环境噪声。
- AGC，Automatic Gain Control，自动增益控制，让音量更稳定。
- VAD，Voice Activity Detection，语音活动检测，判断当前是否有人声。

音频处理通常发生在编码前。AEC 对设备延迟、采样同步和播放参考信号很敏感，实际调试时要特别关注声卡缓冲和系统音频路径。

## 16. 实时音视频架构对比

常见架构：

- P2P：客户端之间直接传媒体，适合 1 对 1 通话。优点是延迟低、服务端成本低；缺点是多人场景扩展差，NAT 穿透不稳定。
- MCU：服务器接收多路媒体后混音混画，再发给客户端。优点是客户端压力小、下行带宽低；缺点是服务器 CPU 压力大，延迟更高。
- SFU：服务器只转发媒体，不做完整混合。优点是扩展性好、服务端 CPU 相对低、适合多人会议；缺点是客户端下行带宽和解码压力更高。

## 17. SFU 工作原理

SFU，Selective Forwarding Unit，选择性转发单元。

工作流程：

```text
客户端 A/B/C 发布媒体流到 SFU
  -> SFU 接收 RTP/RTCP
  -> SFU 根据订阅关系转发给其他客户端
  -> 客户端按需订阅不同用户、不同清晰度或不同 simulcast 层
```

SFU 的关键能力：

- 发布和订阅管理。
- RTP 转发。
- RTCP 反馈转发或聚合。
- Simulcast / SVC 多层码流选择。
- 说话人检测。
- 弱网下的降级策略。

主流 SFU：

- Mediasoup：高性能、可扩展、API 丰富，适合大规模应用。
- Janus：轻量级、插件化，适合定制开发。
- SRS：简单易用，支持多种协议，适合直播和实时音视频结合场景。

## 18. 对应学习 Demo

WebRTC 相关 Demo 放在 `WebRTC-Demos` 目录下。当前第一阶段项目是“自研 WebSocket 信令服务器 + 跨网 P2P 穿透”，完整项目目标、技术栈、信令协议和实现顺序见 `WebRTC-Demos/README.md`。

当前目录对应：

```text
WebRTC-Demos/
  -> P2P-Signaling-Server/    Node.js WebSocket 信令服务器
  -> Cpp-WebRTC-Client/       C++ 客户端接入路线和后续代码
```

如果后续参与 SRS 开源贡献，WebRTC 方向可以重点关注 SRS 中的信令、SDP、ICE、DTLS、SRTP、RTP/RTCP 和 SFU 转发链路。建议先完成 `WebRTC-Demos` 的 P2P 信令项目，再对照 `Notes/srs-open-source-contribution-roadmap.md` 去读 SRS WebRTC 模块。
