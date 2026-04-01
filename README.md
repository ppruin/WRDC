这个项目主要是为了在局域网中方便的控制其他设备，仅从浏览器端即可控制设备。目前仅保证支持 Windows 设备之间的一些简单控制

# 依赖
- mimalloc 的 VS 工程源码
- ImGUI 的 DX12 版本的 VS 工程源码
- vcpkg 进行了部分包管理器，以下是 vcpkg.json
  ```
  {
    "dependencies": [
      {
        "name": "uwebsockets",
        "features": [ "ssl", "zlib" ]
      },
      {
        "name": "libdatachannel",
        "features": [ "srtp" ]
      },
      {
        "name": "x264",
        "features": [ "chroma-format-all" ]
      },
      {
        "name": "openh264"
      }
    ]
  }
  ```


# 项目执行流程示意 

```mermaid
flowchart TD
    A["rdc.exe\nentry.cpp"] -->|无参数 / gui| G["GUI 模式\nui/gui_main.cpp"]
    A -->|server| S["服务端入口\nserver/main.cpp"]
    A -->|host| H["主机端入口\nagent/main.cpp"]
    A -->|controller| C["原生控制端入口\ncontroller/main.cpp"]

    G --> G1["启动托管 server 子进程"]
    G --> G2["启动托管 host 子进程"]
    G --> G3["打开浏览器控制页"]
    G1 --> S
    G2 --> H
    G3 --> B["浏览器控制端\ncontroller/ui/browser_controller_assets.cpp"]

    S --> S1["SignalingServer::Run"]
    S1 --> S2["HTTP: / /index.html /controller.js /healthz"]
    S1 --> S3["WebSocket: /signal"]
    S3 --> H1["host 注册 / 会话 / 信令"]
    S3 --> C1["native controller 注册 / 会话 / 信令"]
    S3 --> B1["browser controller 注册 / 会话 / 信令"]

    H --> H2["HostClient::Run"]
    H2 --> H3["WinWebSocketClient 注册 device"]
    H2 --> H4["DesktopStreamer 采集桌面"]
    H4 --> H5["BGRA -> NV12 -> H.264"]
    H5 --> H6["PeerSession(host) 发送视频"]
    H2 --> H7["控制消息 -> 键鼠注入"]

    C --> C2["ControllerClient::Run"]
    C2 --> C3["WinWebSocketClient 注册 controller"]
    C3 --> C4["create_session"]
    C4 --> C5["PeerSession(controller) 建立 WebRTC"]
    C5 --> C6["接收 RTP/H.264 样本"]
    C6 --> C7["H264RtpDepacketizer 重组访问单元"]

    B --> B2["WebSocket 注册 controller"]
    B2 --> B3["create_session"]
    B3 --> B4["RTCPeerConnection + recvonly video"]
    B4 --> B5["control / control_rt 数据通道"]
    B5 --> H7
    H6 --> B4
```
