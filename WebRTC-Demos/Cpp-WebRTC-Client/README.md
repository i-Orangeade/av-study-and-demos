# Cpp WebRTC Client

本目录用于后续实现 C++ WebRTC Native 客户端。当前先记录客户端职责、信令接入方式和推荐开发顺序。

## 目标

实现一个 C++ 客户端，连接 `P2P-Signaling-Server`，完成：

- 加入房间。
- 交换 SDP Offer/Answer。
- 交换 ICE Candidate。
- 配置公网 STUN。
- 创建 `PeerConnection`。
- 添加本地音视频轨道。
- 1 对 1 P2P 音视频通话。

## 技术选型

- 信令连接：libwebsockets。
- WebRTC：WebRTC Native C++ API。
- 视频渲染：SDL2 或 OpenGL。
- 音频播放：SDL2。
- 自定义视频源：`webrtc::VideoTrackSourceInterface`。
- 自定义视频接收：`webrtc::VideoSinkInterface<webrtc::VideoFrame>`。

## 客户端主流程

```text
启动客户端
  -> 连接 WebSocket 信令服务器
  -> 发送 join
  -> 创建 PeerConnectionFactory
  -> 创建 PeerConnection
  -> 添加本地音频和视频 track
  -> 主叫创建 offer
  -> setLocalDescription
  -> 通过信令发送 offer
  -> 被叫 setRemoteDescription
  -> 被叫创建 answer
  -> 双方交换 candidate
  -> ICE connected
  -> 音视频传输
```

## STUN 配置示例

后续创建 `PeerConnection` 时需要配置 ICE server：

```cpp
webrtc::PeerConnectionInterface::RTCConfiguration config;
webrtc::PeerConnectionInterface::IceServer stun_server;
stun_server.urls.push_back("stun:stun.l.google.com:19302");
config.servers.push_back(stun_server);
```

如果跨网 P2P 失败，尤其是对称型 NAT 场景，需要增加 TURN：

```cpp
webrtc::PeerConnectionInterface::IceServer turn_server;
turn_server.urls.push_back("turn:your-turn-server:3478");
turn_server.username = "user";
turn_server.password = "password";
config.servers.push_back(turn_server);
```

## 信令 JSON

加入房间：

```json
{
  "type": "join",
  "roomId": "room-001",
  "userId": "alice"
}
```

转发 SDP：

```json
{
  "type": "offer",
  "roomId": "room-001",
  "to": "bob",
  "sdp": "v=0..."
}
```

转发 ICE Candidate：

```json
{
  "type": "candidate",
  "roomId": "room-001",
  "to": "bob",
  "candidate": {
    "candidate": "candidate:...",
    "sdpMid": "0",
    "sdpMLineIndex": 0
  }
}
```

## 后续开发拆分

1. 写 libwebsockets 客户端，只验证 `join`、`peer-joined`、`offer`、`answer`、`candidate` 收发。
2. 接入 WebRTC Native，先不采集真实摄像头，用默认音视频设备跑通 PeerConnection。
3. 增加自定义视频源，把本地 YUV/RGB 帧送进 `VideoTrack`。
4. 增加 SDL2/OpenGL 渲染远端视频帧。
5. 增加音频播放和音频处理参数验证。
6. 跨局域网联调，必要时部署 TURN。

## 公网服务器说明

跨局域网测试时，信令服务器需要公网可访问。可以把 `P2P-Signaling-Server` 部署到腾讯云轻量应用服务器，并开放 WebSocket 端口。媒体是否能 P2P 直连取决于 NAT 类型和 ICE 检查结果；信令服务器在线不等于媒体一定能直连。
