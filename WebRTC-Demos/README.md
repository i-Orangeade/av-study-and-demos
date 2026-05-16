# WebRTC Demos

本目录用于记录 WebRTC 相关学习路线和少量配套实验。WebRTC 涉及浏览器 API、Native SDK、ICE/STUN/TURN、DTLS/SRTP、RTP/RTCP、拥塞控制、音频处理和 SFU 服务端等大量工程细节，因此更推荐优先通过官方资料和成熟开源仓库学习完整知识体系与 Demo。本目录中的内容主要作为个人学习笔记、信令实验和后续 C++ Native 客户端验证入口。

## 推荐学习资料

- [WebRTC 官方网站](https://webrtc.org/)：WebRTC 总入口，适合了解能力边界、架构和 Native 开发资料。
- [MDN WebRTC API](https://developer.mozilla.org/en-US/docs/Web/API/WebRTC_API)：浏览器 WebRTC API 文档，适合学习 `RTCPeerConnection`、`MediaStream`、`RTCDataChannel` 等接口。
- [WebRTC 官方 Samples](https://webrtc.github.io/samples/) / [GitHub 仓库](https://github.com/webrtc/samples)：官方浏览器 Demo，适合动手理解采集、Offer/Answer、ICE、DataChannel、屏幕共享等流程。
- [WebRTC Native 源码](https://webrtc.googlesource.com/src)：官方 Native 源码入口，适合深入 C++ 接口、模块实现和构建方式。

## 推荐开源仓库

- [Pion WebRTC](https://github.com/pion/webrtc)：Go 语言 WebRTC 实现，适合学习协议层、服务端 WebRTC 和可读性较强的工程组织。
- [aiortc](https://github.com/aiortc/aiortc)：Python WebRTC 实现，适合快速实验信令、媒体轨道、DataChannel 和小型原型。
- [libdatachannel](https://github.com/paullouisageneau/libdatachannel)：C/C++ WebRTC DataChannel、Media Transport 实现，适合学习轻量级 Native 接入。
- [Janus Gateway](https://github.com/meetecho/janus-gateway)：经典 WebRTC Gateway，适合学习插件化网关、视频房间和媒体转发。
- [mediasoup](https://github.com/versatica/mediasoup)：Node.js/C++ SFU，适合学习现代 SFU 架构和多人实时音视频服务端。
- [LiveKit](https://github.com/livekit/livekit)：生产级实时音视频平台，适合学习房间、发布订阅、SFU、SDK 和云原生部署。

## 当前项目

```text
P2P-Signaling-Server/
  Node.js + ws 实现的最小 WebSocket 信令服务器

Cpp-WebRTC-Client/
  C++ WebRTC Native 客户端接入路线、信令协议说明和后续实现入口
```

## 第一阶段目标

项目：自研 WebSocket 信令服务器 + 跨网 P2P 穿透学习实验。

建议先通过官方 Samples 理解浏览器端最小通话流程，再手写一个简单信令服务，验证跨路由器、跨局域网的 1 对 1 P2P 音视频通话。手写信令的目的不是替代成熟方案，而是帮助理解 SDP、Offer/Answer 和 ICE Candidate 在应用层如何交换。

```text
自研 WebSocket 信令服务
  -> C++ 客户端接入信令
  -> 使用公网 STUN 收集候选地址
  -> 交换 SDP Offer/Answer
  -> 交换 ICE Candidate
  -> 建立跨网 P2P 音视频通话
```

## 技术栈

- 信令服务：Node.js + `ws`，开发最快，适合作为第一版信令服务。
- 客户端：C++ + libwebsockets 对接信令，后续接入 WebRTC Native。
- 穿透：配置公网 STUN 服务器；如果遇到对称型 NAT，再补 TURN。

## 信令协议

信令协议采用 JSON 格式，第一版只保留 5 种指令：

- `join`：加入房间。
- `leave`：离开房间。
- `offer`：发起通话，转发 SDP Offer。
- `answer`：响应通话，转发 SDP Answer。
- `candidate`：交换 ICE Candidate。

示例：

```json
{
  "type": "join",
  "roomId": "room-001",
  "userId": "alice"
}
```

```json
{
  "type": "offer",
  "roomId": "room-001",
  "from": "alice",
  "to": "bob",
  "sdp": "v=0..."
}
```

目标能力：

- 多房间机制。
- 用户加入和离开通知。
- `offer` / `answer` / `candidate` 自动转发。
- 配置公网 STUN 后进行跨局域网 P2P 连接测试。
- 后续接入 C++ WebRTC Native，实现 1 对 1 音视频通话。
- 音视频流畅通话。

## 推荐实现顺序

```text
启动 Node.js 信令服务器
  -> 用两个 WebSocket 客户端验证 join/leave
  -> 验证 offer/answer/candidate 转发
  -> C++ libwebsockets 接入信令
  -> WebRTC Native 创建 PeerConnection
  -> 配置 STUN 并交换 ICE Candidate
  -> 添加本地音视频轨道
  -> 完成跨网 1 对 1 通话
```

## 公网服务器使用时机

同一台机器或同一局域网测试时，信令服务器可以先跑在本机。

跨路由器、跨局域网测试时，需要把 `P2P-Signaling-Server` 部署到公网服务器，例如腾讯云轻量应用服务器，并开放 WebSocket 服务端口。STUN 可以先使用公网 STUN，若遇到对称型 NAT 或企业网络，再增加 TURN。
