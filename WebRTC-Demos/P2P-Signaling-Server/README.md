# P2P Signaling Server

这是 WebRTC 学习项目的第一步：使用 Node.js + `ws` 实现一个最小可用的 WebSocket 信令服务器。

信令服务器只负责转发控制消息，不负责转发音视频媒体数据。

## 信令类型

当前保留 5 种 JSON 指令：

- `join`：加入房间。
- `leave`：离开房间。
- `offer`：转发 SDP Offer。
- `answer`：转发 SDP Answer。
- `candidate`：转发 ICE Candidate。

## 消息格式

加入房间：

```json
{
  "type": "join",
  "roomId": "room-001",
  "userId": "alice"
}
```

发送 Offer：

```json
{
  "type": "offer",
  "roomId": "room-001",
  "from": "alice",
  "to": "bob",
  "sdp": "v=0..."
}
```

发送 Candidate：

```json
{
  "type": "candidate",
  "roomId": "room-001",
  "from": "alice",
  "to": "bob",
  "candidate": {
    "candidate": "candidate:...",
    "sdpMid": "0",
    "sdpMLineIndex": 0
  }
}
```

## 运行方式

```bash
npm install
npm start
```

默认监听端口是 `8080`，可以通过环境变量修改：

```bash
PORT=9000 npm start
```

## 跨网测试

本机测试时可以连接：

```text
ws://127.0.0.1:8080
```

跨局域网测试时，需要部署到公网服务器，并开放对应 TCP 端口：

```text
ws://your-public-ip:8080
```

后续如果使用 HTTPS 页面发起 WebRTC，浏览器侧通常需要 `wss://`，也就是给信令服务器加 TLS 或放到 Nginx HTTPS 反向代理后面。
