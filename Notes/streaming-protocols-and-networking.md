# 流媒体协议与网络编程知识库

本文档用于整理学习 Demo 中涉及到的网络编程、TCP、HTTP 下载和后续流媒体协议相关知识点。当前主要对应 `Demos/AV-Net-TCP-Echo` 和 `Demos/HTTP-File-Downloader`。

## 1. TCP 基础

TCP 是面向连接的可靠字节流协议。它不保留应用层消息边界，只保证字节按顺序、可靠地到达。

这意味着应用层需要自己决定如何切分消息：

- Echo Demo 中客户端发送一行文本，并按发送长度等待回显。
- HTTP 中通过响应头、`Content-Length`、`Transfer-Encoding` 等字段确定响应体边界。
- 自定义协议中常见做法是增加固定长度消息头，例如 `length + body`。

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

## 9. 后续可扩展方向

- 为 HTTP 下载器增加 301/302 重定向处理。
- 支持 `Transfer-Encoding: chunked`。
- 增加超时控制和下载速度统计。
- 实现多线程分片下载。
- 基于 HTTP 下载器继续解析 HLS `.m3u8`。
- 对比 HTTP-FLV、HLS、DASH、RTMP、RTSP 的连接模型和延迟特征。
