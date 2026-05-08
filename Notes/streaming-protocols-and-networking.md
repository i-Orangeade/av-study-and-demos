# 流媒体协议与网络编程知识库

本文档用于整理学习 Demo 中涉及到的网络编程、TCP、UDP、HTTP 下载和流媒体协议相关知识点。当前主要对应 `Network-Demos` 下的 TCP Echo、HTTP 下载、UDP 广播、RTMP、HTTP-FLV、HLS、RTSP 和 RTP 学习项目。

## 1. TCP 基础

TCP 是面向连接的可靠字节流协议。它不保留应用层消息边界，只保证字节按顺序、可靠地到达。

这意味着应用层需要自己决定如何切分消息：

- Echo Demo 中客户端发送一行文本，并按发送长度等待回显。
- HTTP 中通过响应头、`Content-Length`、`Transfer-Encoding` 等字段确定响应体边界。
- 自定义协议中常见做法是增加固定长度消息头，例如 `length + body`。

## 1.1 网络模型与地址基础

OSI 七层模型常用于理解网络分层：

```text
应用层
表示层
会话层
传输层
网络层
数据链路层
物理层
```

实际工程中更常用 TCP/IP 四层模型：

```text
应用层：HTTP、RTMP、RTSP、HLS、DNS
传输层：TCP、UDP
网络层：IP、ICMP
网络接口层：以太网、Wi-Fi
```

常见基础概念：

- IP 地址：标识网络中的一台主机或一个接口。
- 端口号：标识主机上的一个应用进程，TCP/UDP 都有端口概念。
- 子网掩码：用于判断两个 IP 是否在同一个网段。
- 默认网关：跨网段通信时数据包通常先发给网关。

字节序也很重要。网络协议通常使用大端序，也叫网络字节序：

- `htons`：host to network short，常用于端口。
- `htonl`：host to network long，常用于 IPv4 地址或 32 bit 字段。
- `ntohs`：network to host short。
- `ntohl`：network to host long。

## 1.2 TCP 连接、窗口与拥塞控制

TCP 建立连接使用三次握手：

```text
Client -> SYN
Server -> SYN + ACK
Client -> ACK
```

TCP 断开连接通常是四次挥手：

```text
主动关闭方 -> FIN
被动关闭方 -> ACK
被动关闭方 -> FIN
主动关闭方 -> ACK
```

TCP 的可靠性主要来自：

- 序列号：标识字节流中的位置。
- ACK：确认已经收到的数据。
- 重传：丢包后重新发送。
- 滑动窗口：控制发送方可以连续发送多少未确认数据。
- 拥塞控制：根据网络拥塞程度调节发送速率，常见机制包括慢启动、拥塞避免、快速重传和快速恢复。

TCP 适合可靠传输场景，例如 HTTP、RTMP、RTSP 控制连接。它的代价是连接维护、重传和拥塞控制会增加延迟和复杂度。

## 1.3 UDP 特点与适用场景

UDP 是无连接、不保证可靠到达、不保证顺序的传输层协议。

特点：

- 没有连接建立过程。
- 报文边界明确，一次 `sendto` 对应一个 UDP datagram。
- 协议开销小，延迟低。
- 丢包、乱序、重复需要应用层自己处理。

适用场景：

- RTP 音视频实时传输。
- 局域网广播或组播发现。
- DNS 查询。
- 对实时性要求高、可容忍少量丢包的业务。

相关 Demo：

- `Network-Demos/UDP-Broadcast`
- `Network-Demos/RTP-Packet-Parser`

## 2. TCP 服务端基本流程

`AV-Net-TCP-Echo` 的服务端流程可以概括为：

```text
socket
  -> setsockopt(SO_REUSEADDR)
  -> bind
  -> listen
  -> epoll_wait
  -> accept / recv / send
```

关键 API：

- `socket`：创建 TCP 套接字。
- `setsockopt(SO_REUSEADDR)`：方便服务端重启后快速重新绑定端口。
- `bind`：把 socket 绑定到本机 IP 和端口。
- `listen`：让 socket 进入监听状态。
- `accept`：接收客户端连接，得到新的客户端 fd。
- `recv`：从连接读取数据。
- `send`：向连接写入数据。
- `close`：关闭 fd。

## 2.1 C++ Socket 编程基础

Socket 可以理解为应用程序访问网络协议栈的文件描述符。在 Linux 中，socket fd 和普通文件 fd 一样可以 `read`、`write`、`close`，但它背后连接的是网络协议栈。

常见 socket 类型：

- `SOCK_STREAM`：字节流，通常对应 TCP。
- `SOCK_DGRAM`：数据报，通常对应 UDP。
- `AF_INET`：IPv4。
- `AF_INET6`：IPv6。

TCP 服务端典型流程：

```text
socket
  -> bind
  -> listen
  -> accept
  -> recv/send
  -> close
```

TCP 客户端典型流程：

```text
socket
  -> connect
  -> send/recv
  -> close
```

UDP 典型流程：

```text
socket
  -> bind
  -> recvfrom
```

```text
socket
  -> sendto
```

阻塞 IO 与非阻塞 IO：

- 阻塞 IO：没有数据或不能写时，系统调用会卡住当前线程。
- 非阻塞 IO：没有数据或不能写时立即返回 `EAGAIN` / `EWOULDBLOCK`。
- IO 多路复用：用一个线程同时等待多个 fd，常见 API 有 `select`、`poll`、`epoll`。

`epoll` 是 Linux 下高性能网络服务常用方案，适合大量连接。需要重点理解：

- `epoll_create1`：创建 epoll 实例。
- `epoll_ctl`：添加、修改或删除 fd。
- `epoll_wait`：等待事件发生。
- `EPOLLIN`：可读。
- `EPOLLOUT`：可写。
- `EPOLLERR` / `EPOLLHUP`：异常或挂起。

## 3. 非阻塞 IO 与 epoll

一个服务端要同时处理多个客户端时，不能让某个客户端的 `accept`、`recv` 或 `send` 长时间阻塞整个进程。

所以 Echo Demo 会把监听 fd 和客户端 fd 设置为非阻塞：

```cpp
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

非阻塞 fd 暂时没有数据可读或暂时不能写时，系统调用会返回 `-1`，并设置 `errno` 为 `EAGAIN` 或 `EWOULDBLOCK`。这不是致命错误，而是表示当前还没准备好，稍后再试。

`epoll` 的作用是告诉程序哪些 fd 已经准备好：

- `EPOLLIN`：可以读，例如有新连接或客户端发来了数据。
- `EPOLLOUT`：可以写，例如 socket 发送缓冲区有空间。
- `EPOLLRDHUP`：对端关闭连接。
- `EPOLLERR` / `EPOLLHUP`：连接异常或挂起。

Echo Demo 的主循环可以概括为：

```text
epoll_wait 等待事件
  -> 如果是监听 fd，循环 accept 新连接
  -> 如果是客户端 fd，recv 读取数据
  -> 把收到的数据放入 pendingOutput
  -> 等 fd 可写时 send 回客户端
```

## 4. 为什么需要发送缓冲

`send` 不保证一次把所有数据都写出去。尤其在非阻塞模式下，可能只发送了一部分，也可能暂时不能发送。

所以每个客户端都需要维护自己的待发送数据：

```cpp
struct ClientState {
    std::string pendingOutput;
};
```

当 `recv` 收到数据时，把数据追加到 `pendingOutput`；当 `EPOLLOUT` 到来时，再尽量把 `pendingOutput` 里的数据发出去。这个思路在 HTTP 服务端、RTMP 连接、WebSocket 服务等长连接场景中也很常见。

## 5. HTTP 下载基础

HTTP 是应用层协议，底层通常跑在 TCP 之上。一个最简单的 HTTP 下载流程是：

```text
解析 URL
  -> DNS 解析 host
  -> TCP connect
  -> 发送 GET 请求
  -> 读取响应头
  -> 根据状态码和响应头保存响应体
```

`HTTP-File-Downloader` 当前发送的是 HTTP/1.1 请求：

```text
GET /path/file.mp4 HTTP/1.1
Host: example.com
User-Agent: av-study-http-downloader/1.0
Accept: */*
Connection: close
```

`Connection: close` 表示服务端响应完成后关闭连接。对于学习 Demo 来说，这样可以把连接关闭作为响应体结束的信号之一，降低实现复杂度。

## 5.1 HTTP/HTTPS 协议基础

常见 HTTP 请求方法：

- `GET`：获取资源，例如下载文件、拉取 M3U8。
- `POST`：提交数据，例如表单、接口请求。
- `PUT`：上传或替换资源。
- `DELETE`：删除资源。

常见 HTTP 状态码：

- `200 OK`：请求成功。
- `301 Moved Permanently`：永久重定向。
- `302 Found`：临时重定向。
- `400 Bad Request`：请求格式错误。
- `404 Not Found`：资源不存在。
- `500 Internal Server Error`：服务端错误。

常见请求头和响应头：

- `Content-Length`：响应体或请求体长度。
- `Content-Type`：数据类型，例如 `video/x-flv`、`application/vnd.apple.mpegurl`。
- `Range`：范围请求，用于断点续传。
- `Content-Range`：服务端返回的范围响应描述。
- `Transfer-Encoding: chunked`：分块传输。

HTTPS 可以理解为 HTTP over TLS。它在 HTTP 之前先完成 TLS 握手：

```text
TCP connect
  -> TLS ClientHello / ServerHello
  -> 证书校验
  -> 密钥协商
  -> 加密 HTTP 数据传输
```

当前学习 Demo 主要手写明文 HTTP，目的是先理解协议文本和 TCP 收发流程。HTTPS 涉及证书、加密套件和 TLS 状态机，实际工程一般使用 OpenSSL、BoringSSL、mbedTLS 或 libcurl。

## 6. Content-Length 与响应体边界

HTTP 响应通常由状态行、响应头和响应体组成：

```text
HTTP/1.1 200 OK
Content-Length: 12345
Content-Type: application/octet-stream

<body bytes>
```

`Content-Length` 表示响应体字节数。下载器可以用它计算总大小和下载进度。

如果服务端使用 `Transfer-Encoding: chunked`，响应体会被拆成多个 chunk，每个 chunk 前面都有长度字段。当前 Demo 为了保持简单没有实现 chunked 解析，遇到 chunked 响应会直接提示不支持。

## 7. 断点续传与 Range 请求

HTTP 断点续传依赖 `Range` 请求头。假设本地已经有 1000 字节，客户端可以请求：

```text
Range: bytes=1000-
```

如果服务端支持断点续传，通常会返回：

```text
HTTP/1.1 206 Partial Content
Content-Range: bytes 1000-9999/10000
Content-Length: 9000
```

此时客户端以 append 模式打开本地文件，把后续响应体追加到已有文件末尾。

如果服务端不支持 Range，可能返回 `200 OK` 和完整文件。此时客户端不能继续 append，否则文件会重复，需要从头覆盖下载。`HTTP-File-Downloader` 中就是按这个规则处理 `200` 和 `206`。

## 8. HTTP 与流媒体协议的关系

很多现代流媒体协议都建立在 HTTP 之上，例如 HLS 和 MPEG-DASH。

HLS 的常见结构：

```text
master.m3u8
  -> media playlist .m3u8
  -> segment0.ts / segment0.m4s
  -> segment1.ts / segment1.m4s
```

播放器本质上会不断下载 playlist 和媒体分片，再交给解封装、解码和渲染模块处理。

因此，理解 HTTP 下载、Range、状态码、响应头和 TCP 连接，是继续学习 HLS、DASH、HTTP-FLV 等流媒体协议的基础。

## 9. UDP 广播

UDP 广播用于在局域网内向一组主机发送无连接报文。发送端需要设置 `SO_BROADCAST`，然后向 `255.255.255.255` 或网段定向广播地址发送数据。

相关 Demo：

- `Network-Demos/UDP-Broadcast`

核心链路：

```text
socket(AF_INET, SOCK_DGRAM)
  -> setsockopt(SO_BROADCAST)
  -> sendto broadcast address
  -> recvfrom on receiver
```

广播通常只在同一个二层广播域内传播，跨网段是否可达取决于路由器配置。

## 10. RTMP 基础

RTMP 基于 TCP，常见于传统直播推拉流。学习 RTMP 时可以先拆成三层：

- 握手：C0/C1/S0/S1/S2/C2。
- 控制和命令：`connect`、`createStream`、`play`、`publish` 等 AMF0 命令。
- Chunk/message：RTMP 把 message 切成 chunk 传输，接收端需要按 chunk stream 重组。

相关 Demo：

- `Network-Demos/RTMP-Handshake`
- `Network-Demos/RTMP-FLV-Recorder`

学习版录制器已经展示 `play` 命令链路，但要生成标准可播放 FLV，还需要完整实现 RTMP Chunk 分片重组和 message 到 FLV Tag 的转换。

RTMP URL 常见格式：

```text
rtmp://host:1935/app/stream
```

连接流程：

```text
TCP connect
  -> C0 + C1
  -> S0 + S1 + S2
  -> C2
  -> connect
  -> createStream
  -> play / publish
```

RTMP Chunk 机制：

- RTMP message 可以被拆成多个 chunk 传输。
- Chunk Basic Header 包含 chunk type 和 chunk stream id。
- Chunk Message Header 根据 type 不同有 0、3、7、11 字节等长度。
- 接收端必须按 chunk stream id 保存历史 header，并按 message length 拼完整 message。

常见 RTMP message 类型：

- 协议控制消息：Set Chunk Size、Acknowledgement、Window Acknowledgement Size。
- 命令消息：`connect`、`createStream`、`play`、`publish`。
- 数据消息：metadata。
- 音频消息：AAC/MP3 等音频数据。
- 视频消息：H.264/H.265 等视频数据。

RTMP 的优点是基于长连接、推流链路成熟、延迟较低；缺点是基于私有历史协议、HTTP 穿透性不如 HTTP-FLV/HLS，浏览器原生支持较差。RTMP 延迟低的主要原因是服务端可以在一个 TCP 长连接上持续推送小块音视频数据，不需要像 HLS 那样等待切片生成和轮询 playlist。

## 11. HTTP-FLV 与 HLS

HTTP-FLV 和 HLS 都运行在 HTTP 之上，但传输模型不同：

- HTTP-FLV：一个长连接持续传输 FLV Tag，延迟较低，常用于直播。
- HLS：通过 M3U8 索引文件组织多个 TS/fMP4 切片，兼容性好，但延迟通常更高。

相关 Demo：

- `Network-Demos/HTTP-FLV-Player`
- `Network-Demos/M3U8-Parser`
- `Network-Demos/HLS-Player`

HTTP-FLV 的核心是边下载边解析 FLV Tag；HLS 的核心是反复获取 playlist、下载切片、解封装和连续播放。

HTTP-FLV URL 常见格式：

```text
http://host:port/app/stream.flv
```

工作流程：

```text
HTTP GET
  -> HTTP 200 OK
  -> Content-Type: video/x-flv
  -> FLV Header
  -> FLV Tag
  -> FLV Tag
  -> ...
```

HTTP-FLV 和 RTMP 对比：

- RTMP：基于 TCP 私有协议，延迟低，传统直播推流常见。
- HTTP-FLV：基于 HTTP，穿透性更好，容易经过 CDN 和代理，服务端和客户端接入成本较低。
- HTTP-FLV 延迟通常略高于 RTMP，但明显低于传统大切片 HLS。
- 很多平台使用 HTTP-FLV，是因为它兼顾较低延迟和 HTTP 基础设施兼容性。

HLS URL 常见格式：

```text
http://host:port/app/stream.m3u8
```

HLS 工作原理：

```text
下载 m3u8
  -> 解析 EXT-X-* 标签
  -> 获取 TS/fMP4 切片 URL
  -> 下载切片
  -> 解封装、解码、播放
  -> 直播场景循环刷新 m3u8
```

常见 M3U8 标签：

- `EXT-X-VERSION`：HLS 协议版本。
- `EXT-X-TARGETDURATION`：切片最大时长。
- `EXTINF`：单个媒体切片时长和描述。
- `EXT-X-STREAM-INF`：主 playlist 中的子码率流描述。
- `EXT-X-ENDLIST`：点播列表结束标记，直播通常没有。

HLS 自适应码率依赖主 M3U8 中的多路子 playlist。播放器会根据带宽、缓冲和解码能力选择不同清晰度，并在播放过程中切换。

## 12. RTSP 与 RTP

RTSP 是控制协议，负责 `OPTIONS`、`DESCRIBE`、`SETUP`、`PLAY`、`TEARDOWN` 等交互；真正的媒体数据通常通过 RTP 承载。

相关 Demo：

- `Network-Demos/RTSP-Client`
- `Network-Demos/RTP-Packet-Parser`

RTSP 客户端首先通过 DESCRIBE 获取 SDP，再通过 SETUP 建立 RTP 传输通道。RTP 包解析器负责解析序列号、时间戳、payload type，并按 H.264 RTP 封装规则重组 NALU。

RTSP URL 常见格式：

```text
rtsp://host:554/stream
```

RTSP 交互流程：

```text
OPTIONS
  -> DESCRIBE
  -> SETUP
  -> PLAY
  -> PAUSE
  -> TEARDOWN
```

SDP 用来描述媒体会话，常见字段：

- `m=`：媒体类型、端口、传输协议和 payload type。
- `a=rtpmap`：payload type 到编码格式的映射。
- `a=fmtp`：编码参数，例如 H.264 的 SPS/PPS。
- `a=control`：RTSP track 控制 URL。

RTP 包核心字段：

- version：RTP 版本，通常是 2。
- payload type：负载类型，例如动态 H.264 常见 96，AAC 常见 97。
- sequence number：序列号，用于发现丢包和乱序。
- timestamp：媒体时间戳，用于播放同步。
- SSRC：同步源标识。

H.264 RTP 封装常见模式：

- Single NALU：一个 RTP 包携带一个完整 NALU。
- FU-A：一个较大的 NALU 被拆分到多个 RTP 包，需要按 start/end 标志重组。

RTCP 是 RTP 的控制协议，用于质量反馈和同步信息：

- SR：Sender Report，发送者报告。
- RR：Receiver Report，接收者报告。
- 可用于统计丢包率、抖动、时钟映射等 QoS 信息。

## 13. 建议学习顺序

```text
AV-Net-TCP-Echo
  -> HTTP-File-Downloader
  -> UDP-Broadcast
  -> M3U8-Parser
  -> FLV-Parser
  -> HTTP-FLV-Player
  -> RTMP-Handshake
  -> RTMP-FLV-Recorder
  -> RTSP-Client
  -> RTP-Packet-Parser
  -> HLS-Player
```

先理解 TCP/UDP 和 HTTP，再进入容器格式与流媒体协议，会更容易看清每一层负责什么。

## 14. 后续可扩展方向

- 为 HTTP 下载器增加 301/302 重定向处理。
- 支持 `Transfer-Encoding: chunked`。
- 增加超时控制和下载速度统计。
- 实现多线程分片下载。
- 基于 HTTP 下载器继续解析 HLS `.m3u8`。
- 对比 HTTP-FLV、HLS、DASH、RTMP、RTSP 的连接模型和延迟特征。
