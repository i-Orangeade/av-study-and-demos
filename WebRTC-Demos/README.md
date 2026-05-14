# WebRTC Demos

本目录用于存放 WebRTC 相关学习 Demo，目标是从自研信令服务器开始，逐步完成跨网 P2P 音视频通话、自定义音视频源、RTP/RTCP 分析和 SFU 接入实验。

## 当前项目

```text
P2P-Signaling-Server/
  Node.js + ws 实现的最小 WebSocket 信令服务器

Cpp-WebRTC-Client/
  C++ WebRTC Native 客户端接入路线、信令协议说明和后续实现入口
```

## 第一阶段目标

项目：自研 WebSocket 信令服务器 + 跨网 P2P 穿透。

不用官方自带信令服务，自己手写信令服务，实现跨路由器、跨局域网的 1 对 1 P2P 音视频通话。

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
