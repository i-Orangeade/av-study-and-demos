# SRS 开源贡献路线与简历提升计划

本文档用于把当前 `AV-study-and-demos` 的学习成果，过渡到 SRS 开源贡献和音视频开发岗位简历项目。目标不是“刷一个 PR”，而是通过真实开源项目证明自己能读懂工程、定位问题、补文档、写测试、做小功能，并能在面试中讲清楚技术判断。

## 1. 为什么选择 SRS

SRS，Simple Realtime Server，是一个高性能实时媒体服务器，覆盖的方向和当前学习项目高度匹配：

- RTMP 推拉流。
- HTTP-FLV 直播播放。
- HLS 切片和分发。
- WebRTC 实时音视频。
- RTP/RTCP、ICE、DTLS、SRTP 等实时通信基础。
- H.264、H.265、AAC、Opus 等常见音视频格式。

对你现在的阶段来说，SRS 的价值有三点：

- 它是真实生产级工程，比学习 Demo 更接近企业工作。
- 它覆盖音视频客户端、服务端、协议、网络和 WebRTC，多条岗位方向都能复用。
- 开源贡献可以写进简历，证明你不是只会看教程，而是能参与真实协作。

## 2. 当前基础是否适合开始贡献

如果你已经能独立讲清楚当前项目中的 Demo，已经可以开始接触 SRS，但要先从低风险贡献开始。

你已有基础：

- FFmpeg 解封装、解码、转码和 SDL2 播放链路。
- TCP/UDP、epoll、HTTP、RTMP、HTTP-FLV、HLS、RTSP/RTP。
- FLV、M3U8、TS、RTP 包结构。
- WebRTC 的 PeerConnection、SDP、ICE、STUN/TURN、RTP/RTCP。
- Node.js WebSocket 信令服务器雏形。

还需要补的工程能力：

- 阅读大型 C++ 项目结构。
- 熟悉 SRS 编译、运行、配置文件和日志。
- 能根据日志定位模块和代码路径。
- 会写最小复现步骤。
- 会提交小而清晰的 PR。
- 会和维护者沟通，不把问题描述成“跑不起来”，而是给出环境、命令、日志和预期行为。

## 3. 不建议一开始做什么

初学者最容易踩坑的是一上来改核心功能。

暂时不建议：

- 直接改 WebRTC 核心传输逻辑。
- 直接重构 RTMP/HLS 主链路。
- 直接改拥塞控制、JitterBuffer、SRTP 等复杂模块。
- 一次提交大量格式化或无关重构。
- 为了贡献而贡献，提交没有实际价值的改动。

更适合从这些方向开始：

- 文档修正和补充。
- 示例配置补充。
- README 中过时命令修正。
- 日志信息优化。
- 错误提示优化。
- 小工具或脚本修复。
- 测试用例补充。
- 复现 issue 并补充排查结论。

## 4. 第一阶段：把 SRS 跑起来

目标：本地能编译、运行、推流、播放，并知道每一步对应哪个协议。

建议任务：

```text
拉取 SRS 源码
  -> 阅读 README 和 CONTRIBUTING
  -> 本地编译
  -> 启动最小配置
  -> 用 FFmpeg 推 RTMP
  -> 用 ffplay / VLC 播放 RTMP
  -> 播放 HTTP-FLV
  -> 播放 HLS
  -> 记录命令、配置、日志和问题
```

需要整理进自己的笔记：

- SRS 启动命令。
- 配置文件路径。
- 监听端口。
- RTMP 推流 URL。
- HTTP-FLV 播放 URL。
- HLS 播放 URL。
- 推流失败时日志怎么看。
- 播放失败时先查哪几层。

验收标准：

- 你能在 5 分钟内从零启动 SRS 并完成一次 RTMP 推流和 HTTP-FLV/HLS 播放。
- 你能解释 RTMP 推流进入 SRS 后，为什么可以用 HTTP-FLV 或 HLS 播放。

## 5. 第二阶段：按协议读代码

不要从 `main` 一路硬读。音视频项目更适合按“协议链路”读。

推荐顺序：

```text
配置加载
  -> 监听端口
  -> RTMP accept
  -> RTMP handshake
  -> connect / createStream / publish / play
  -> FLV tag / HTTP-FLV 输出
  -> HLS segment / m3u8 生成
  -> WebRTC 信令和 ICE/DTLS/SRTP
```

读代码时每次只回答一个问题：

- SRS 在哪里监听 RTMP 端口？
- 客户端连接后在哪里做 RTMP handshake？
- `connect`、`createStream`、`publish`、`play` 分别在哪里处理？
- RTMP message 在哪里转换成 FLV tag？
- HTTP-FLV 是如何持续写响应体的？
- HLS 的 `.ts` 和 `.m3u8` 在哪里生成？
- WebRTC 的 SDP 在哪里解析？
- ICE Candidate 或 DTLS/SRTP 相关逻辑在哪些模块？

每读完一个问题，写 5 行以内的总结，避免变成无效翻代码。

## 6. 第三阶段：寻找第一类贡献

优先级从低风险到高风险：

1. 文档贡献：修复过时命令、补充缺失步骤、改进新手说明。
2. 示例贡献：增加一份更清晰的配置或运行脚本。
3. 日志贡献：让错误日志更准确，方便定位问题。
4. 测试贡献：补充一个能稳定复现的小测试。
5. 小 bug 修复：范围明确、影响面小、有复现步骤。

判断一个 issue 是否适合你：

- 能在本地稳定复现。
- 涉及模块你能讲清楚。
- 修改文件不超过 3 个最好。
- 不需要大范围架构调整。
- 能写清楚验证方法。

如果没有合适的 `good first issue`，可以先做这些：

- 跑官方示例，发现文档和实际命令不一致。
- 阅读 issue，帮忙补充复现信息。
- 对比日志和代码，提出更明确的错误提示。
- 给某个新手容易卡住的流程补文档。

## 7. 第四阶段：提交一个合格 PR

一个合格 PR 应该小、准、可验证。

PR 标题示例：

```text
Fix typo in WebRTC playback guide
Improve error log when RTMP handshake fails
Add example config for HTTP-FLV playback
```

PR 描述建议包含：

```text
## What changed
说明改了什么。

## Why
说明为什么需要改。

## How to verify
列出本地验证命令、输入、输出或截图。

## Related issue
如果关联 issue，写上链接。
```

不要在第一个 PR 里做：

- 顺手格式化大量文件。
- 把多个不相关问题放进一个 PR。
- 没验证就提交。
- 和维护者争论风格问题。

## 8. 和当前项目的对应关系

当前项目可以作为你读 SRS 的前置索引：

- `Network-Demos/RTMP-Handshake`：对应 SRS RTMP handshake、connect、play/publish 链路。
- `Network-Demos/RTMP-FLV-Recorder`：对应 RTMP message 到 FLV tag 的理解。
- `Network-Demos/HTTP-FLV-Player`：对应 SRS HTTP-FLV 输出。
- `Network-Demos/HLS-Player`：对应 SRS HLS m3u8 和 ts 切片。
- `Network-Demos/RTSP-Client` 和 `RTP-Packet-Parser`：对应 RTP/RTCP、GB28181、WebRTC 媒体包基础。
- `WebRTC-Demos/P2P-Signaling-Server`：对应 WebRTC 信令、SDP 和 ICE 交换。
- `Notes/webrtc-core-and-realtime-communication.md`：对应 SRS WebRTC 模块的概念前置。

简历上不要写“学习了很多 Demo”，要写“围绕 SRS 贡献建立了协议复现和验证工具链”。

## 9. 简历表达方式

如果只是学习阶段，可以写：

```text
音视频学习项目 AV-study-and-demos：
实现 FFmpeg 解封装/解码/转码、SDL2 播放、TCP/UDP Socket、RTMP 握手、HTTP-FLV/HLS/RTSP/RTP 解析等 Demo，并整理协议笔记，为阅读 SRS 流媒体服务器源码和参与开源贡献做准备。
```

如果已经完成 SRS PR，可以写：

```text
SRS 开源贡献：
参与 SRS 流媒体服务器开源项目，完成文档/示例/日志/小功能修复 PR。基于 FFmpeg 和 SRS 搭建 RTMP 推流、HTTP-FLV/HLS 播放验证环境，能够根据配置、日志和协议链路定位推拉流问题。
```

如果完成了真实 bug 修复，可以写得更具体：

```text
修复 SRS 中某模块在某场景下的错误提示/异常处理问题，补充复现步骤和验证命令，熟悉 RTMP/HTTP-FLV/HLS/WebRTC 相关链路。
```

## 10. 面试中怎么讲 SRS 贡献

面试官更关心你是否真的做过，而不是 PR 数量。

推荐表达：

- 我先用 FFmpeg 推流和 ffplay/VLC 播放，把 RTMP、HTTP-FLV、HLS 的链路跑通。
- 然后按协议链路读 SRS 代码，不是一上来全量读。
- 我从低风险贡献开始，比如文档、示例、日志和小 bug。
- 每次改动我都会写清楚复现命令、验证步骤和影响范围。
- 这个过程让我理解了真实媒体服务器里配置、日志、协议处理和工程协作的关系。

容易被追问的问题：

- SRS 支持哪些协议？各自适合什么场景？
- RTMP 推流后，为什么 HTTP-FLV 能播放？
- HLS 为什么延迟更高？
- WebRTC 为什么适合低延迟？
- SRS 日志里如何定位一个推流失败问题？
- 你的 PR 改了什么，为什么这样改？

## 11. 30 天行动计划

第 1 周：

- 跑通 SRS 本地编译和启动。
- 完成 RTMP 推流、RTMP 播放、HTTP-FLV 播放、HLS 播放。
- 把命令和日志记录到自己的笔记。

第 2 周：

- 按 RTMP 链路读代码。
- 找到 handshake、connect、publish、play 的处理路径。
- 对照当前 `RTMP-Handshake` 和 `RTMP-FLV-Recorder` Demo 写总结。

第 3 周：

- 按 HTTP-FLV/HLS 链路读代码。
- 找到 FLV 输出、HLS segment、m3u8 生成逻辑。
- 尝试复现一个文档或示例问题。

第 4 周：

- 选择一个低风险 issue 或文档改进点。
- 提交第一个小 PR。
- 整理 PR 背后的技术说明，用于简历和面试。

## 12. 最重要的判断标准

你做 SRS 开源贡献不是为了“装饰简历”，而是为了证明这些能力：

- 能读真实 C++ 工程。
- 能搭建音视频验证环境。
- 能理解协议链路。
- 能从日志定位问题。
- 能写出可被维护者接受的小改动。
- 能清楚解释自己的贡献价值。

做到这些，即使第一个 PR 只是文档或日志优化，也比一个堆砌功能但讲不清楚的个人项目更有说服力。
