/**

 * @file browser_controller_assets.cpp

 * @brief 实现 controller/ui/browser_controller_assets 相关的类型、函数与流程。

 */



#include "browser_controller_assets.hpp"



namespace rdc::controller::ui {



namespace {



constexpr std::string_view kBrowserControllerHtml = R"RDC_HTML(<!doctype html>







<html lang="zh-CN">







<head>







  <meta charset="utf-8">







  <meta name="viewport" content="width=device-width, initial-scale=1">







  <title>RDC 浏览器控制端</title>







  <style>







    :root {







      color-scheme: light;







      --bg: #f6f3eb;







      --panel: rgba(255,255,255,0.86);







      --line: #d8cfbf;







      --ink: #1f2722;







      --muted: #657067;







      --accent: #0f766e;







      --accent-strong: #115e59;







      --danger: #b42318;







    }







    * { box-sizing: border-box; }







    body {







      margin: 0;







      min-height: 100vh;







      font-family: "Microsoft YaHei UI", "PingFang SC", sans-serif;







      color: var(--ink);







      background:







        radial-gradient(circle at top left, rgba(15,118,110,0.16), transparent 34%),







        radial-gradient(circle at bottom right, rgba(180,35,24,0.1), transparent 28%),







        linear-gradient(135deg, #f8f4ea 0%, #efe7d8 100%);







    }







    .shell {







      display: grid;







      grid-template-columns: 360px minmax(0, 1fr);







      gap: 20px;







      padding: 24px;







      min-height: 100vh;







    }







    .panel {







      border: 1px solid var(--line);







      border-radius: 18px;







      background: var(--panel);







      backdrop-filter: blur(12px);







      box-shadow: 0 18px 60px rgba(28, 37, 33, 0.12);







    }







    .sidebar {







      display: flex;







      flex-direction: column;







      padding: 18px;







      gap: 14px;







    }







    .title {







      margin: 0;







      font-size: 28px;







      line-height: 1.1;







    }







    .subtitle {







      margin: 0;







      color: var(--muted);







      font-size: 14px;







      line-height: 1.6;







    }







    .field {







      display: flex;







      flex-direction: column;







      gap: 6px;







    }







    .field label {







      font-size: 13px;







      color: var(--muted);







    }







    .field input {







      width: 100%;







      padding: 12px 14px;







      border-radius: 12px;







      border: 1px solid var(--line);







      background: rgba(255,255,255,0.9);







      color: var(--ink);







      font-size: 14px;







      outline: none;







    }







    .field input:focus {







      border-color: var(--accent);







      box-shadow: 0 0 0 3px rgba(15,118,110,0.15);







    }







    .actions {







      display: grid;







      grid-template-columns: 1fr 1fr;







      gap: 10px;







    }







    button {







      padding: 12px 14px;







      border-radius: 12px;







      border: none;







      cursor: pointer;







      font-size: 14px;







      font-weight: 600;







      transition: transform 120ms ease, opacity 120ms ease, background 120ms ease;







    }







    button:hover { transform: translateY(-1px); }







    button:active { transform: translateY(0); }







    button.primary {







      background: linear-gradient(135deg, var(--accent), var(--accent-strong));







      color: #fff;







    }







    button.secondary {







      background: rgba(31,39,34,0.08);







      color: var(--ink);







    }







    button.danger {







      background: rgba(180,35,24,0.12);







      color: var(--danger);







    }







    button:disabled {







      opacity: 0.55;







      cursor: default;







      transform: none;







    }







    .status {







      padding: 12px 14px;







      border-radius: 12px;







      background: rgba(15,118,110,0.08);







      color: var(--accent-strong);







      font-size: 13px;







      line-height: 1.5;







      min-height: 46px;







    }







    .log {







      min-height: 220px;







      resize: vertical;







      padding: 12px 14px;







      border-radius: 12px;







      border: 1px solid var(--line);







      background: rgba(26, 29, 27, 0.96);







      color: #eef3ef;







      font-size: 12px;







      line-height: 1.55;







      font-family: Consolas, "Cascadia Mono", monospace;







      white-space: pre-wrap;







      overflow: auto;







    }







    .viewer {







      display: flex;







      flex-direction: column;







      padding: 18px;







      gap: 14px;







    }







    .viewer-head {







      display: flex;







      justify-content: space-between;







      align-items: center;







      gap: 14px;







      flex-wrap: wrap;







    }







    .viewer-head h2 {







      margin: 0;







      font-size: 20px;







    }







    .hint {







      color: var(--muted);







      font-size: 13px;







    }







    .stage {







      position: relative;







      min-height: 70vh;







      border-radius: 18px;







      overflow: hidden;







      background: linear-gradient(180deg, #131816 0%, #252a26 100%);







      border: 1px solid rgba(255,255,255,0.06);







    }







    video {







      width: 100%;







      height: 100%;







      display: block;







      object-fit: contain;







      background: #000;







    }







    .overlay {







      position: absolute;







      inset: 0;







      display: grid;







      place-items: center;







      color: rgba(255,255,255,0.75);







      font-size: 16px;







      letter-spacing: 0.04em;







      pointer-events: none;







      transition: opacity 150ms ease;







    }







    .overlay.hidden { opacity: 0; }







    @media (max-width: 1100px) {







      .shell {







        grid-template-columns: 1fr;







      }







      .stage {







        min-height: 52vh;







      }







    }







  </style>







</head>







<body>







  <div class="shell">







    <aside class="panel sidebar">







      <div>







        <h1 class="title">RDC 浏览器控制端</h1>







        <p class="subtitle">在浏览器中直接连接信令服务，通过 WebRTC 接收主机桌面视频流。</p>







      </div>















      <div class="field">







        <label for="userId">控制端用户 ID</label>







        <input id="userId" value="user-web-1" autocomplete="off">







      </div>















      <div class="field">







        <label for="targetDeviceId">目标主机设备 ID</label>







        <input id="targetDeviceId" value="host-1" autocomplete="off">







      </div>















      <div class="field">







        <label for="signalUrl">信令地址</label>







        <input id="signalUrl" autocomplete="off">







      </div>















      <div class="actions">







        <button id="connectBtn" class="primary">开始连接</button>







        <button id="disconnectBtn" class="danger" disabled>断开会话</button>







      </div>















      <div id="statusBox" class="status">等待连接</div>















      <div class="field">







        <label for="logBox">运行日志</label>







        <div id="logBox" class="log"></div>







      </div>







    </aside>















    <main class="panel viewer">







      <div class="viewer-head">







        <h2>远端桌面画面</h2>







        <span class="hint">当前页面支持 HTTP/WS 与 HTTPS/WSS；信令地址会默认跟随当前页面协议。</span>







      </div>







      <div class="stage">







        <video id="remoteVideo" autoplay playsinline></video>







        <div id="videoOverlay" class="overlay">等待远端视频轨</div>







      </div>







    </main>







  </div>







  <script src="/controller.js?v=20260328-3"></script>







</body>







</html>)RDC_HTML";



constexpr std::string_view kBrowserControllerScript = R"RDC_JS((() => {







  const $ = (id) => document.getElementById(id);







  const formatError = (error) => error instanceof Error ? error.message : String(error);















  const elements = {







    userId: $("userId"),







    targetDeviceId: $("targetDeviceId"),







    signalUrl: $("signalUrl"),







    connectBtn: $("connectBtn"),







    disconnectBtn: $("disconnectBtn"),







    statusBox: $("statusBox"),







    logBox: $("logBox"),







    remoteVideo: $("remoteVideo"),







    videoOverlay: $("videoOverlay")







  };















  const wsScheme = window.location.protocol === "https:" ? "wss" : "ws";







  const defaultSignalUrl = `${wsScheme}://${window.location.host}/signal`;







  const query = new URLSearchParams(window.location.search);







  elements.signalUrl.value = query.get("signal") || defaultSignalUrl;







  elements.userId.value = query.get("user") || elements.userId.value;







  elements.targetDeviceId.value = query.get("target") || elements.targetDeviceId.value;







  elements.remoteVideo.muted = true;







  elements.remoteVideo.autoplay = true;







  elements.remoteVideo.playsInline = true;















)RDC_JS"

R"RDC_JS(

  class BrowserController {







    constructor(view) {







      this.view = view;







      this.socket = null;







      this.peer = null;







      this.controlChannel = null;







      this.sessionId = "";







      this.remoteStream = null;







      this.remoteVideoTrack = null;







      this.videoReceiver = null;







      this.connected = false;







      this.statsTimer = null;







      this.lastStatsLine = "";







    }















    log(message) {







      const now = new Date().toLocaleTimeString();







      this.view.logBox.textContent += `[${now}] ${message}\n`;







      this.view.logBox.scrollTop = this.view.logBox.scrollHeight;







    }















    setStatus(message) {







      this.view.statusBox.textContent = message;







    }















    setOverlay(message) {







      this.view.videoOverlay.textContent = message;







    }















    setConnectedState(isConnected) {







      this.connected = isConnected;







      this.view.connectBtn.disabled = isConnected;







      this.view.disconnectBtn.disabled = !isConnected;







    }































    startStatsLoop() {







      this.stopStatsLoop();







      this.statsTimer = window.setInterval(async () => {







        if (!this.peer) return;















        try {







          const reports = this.videoReceiver && typeof this.videoReceiver.getStats === "function"







            ? await this.videoReceiver.getStats()







            : await this.peer.getStats();







          const codecs = new Map();







          let inbound = null;















          reports.forEach((report) => {







            if (report.type === "codec") {







              codecs.set(report.id, report.mimeType || report.sdpFmtpLine || "");







              return;







            }















            if (report.type === "inbound-rtp" && !report.isRemote &&







                (report.kind === "video" || report.mediaType === "video")) {







              inbound = report;







            }







          });















          let line = "";







          if (inbound) {







            const codec = inbound.codecId && codecs.has(inbound.codecId) ? `, codec=${codecs.get(inbound.codecId)}` : "";







            line =



              `浏览器视频统计: packets=${inbound.packetsReceived || 0}, bytes=${inbound.bytesReceived || 0}, ` +



              `decoded=${inbound.framesDecoded || 0}, keyDecoded=${inbound.keyFramesDecoded || 0}, ` +



              `size=${inbound.frameWidth || 0}x${inbound.frameHeight || 0}${codec}`;







          } else {







            const track = this.remoteVideoTrack;







            const video = this.view.remoteVideo;







            line =



              `浏览器视频统计: 尚未看到 inbound-rtp video, trackState=${track ? track.readyState : "none"}, ` +



              `trackMuted=${track ? track.muted : "n/a"}, elementReady=${video.readyState}, ` +



              `paused=${video.paused}, currentTime=${video.currentTime.toFixed(3)}, ` +



              `size=${video.videoWidth || 0}x${video.videoHeight || 0}`;







          }















          if (line !== this.lastStatsLine) {







            this.lastStatsLine = line;







            this.log(line);







          }







        } catch (error) {







          const line = `浏览器视频统计获取失败: ${formatError(error)}`;







          if (line !== this.lastStatsLine) {







            this.lastStatsLine = line;







            this.log(line);







          }







        }







      }, 1000);







    }















    stopStatsLoop() {







      if (this.statsTimer !== null) {







        window.clearInterval(this.statsTimer);







        this.statsTimer = null;







      }







    }















    async connect() {







      if (this.socket !== null) {







        return;







      }















      if (typeof RTCPeerConnection !== "function") {







        this.setStatus("当前浏览器不支持 WebRTC");







        this.log("当前浏览器环境缺少 RTCPeerConnection，无法接收远端视频");







        return;







      }























      this.setOverlay("正在等待远端视频轨");







      this.setStatus("正在建立浏览器控制会话");







      this.log(`连接信令服务: ${this.view.signalUrl.value}`);







      this.setConnectedState(true);















      const socket = new WebSocket(this.view.signalUrl.value);







      this.socket = socket;















      socket.addEventListener("open", () => {







        this.log("信令 WebSocket 已连接");







      });















      socket.addEventListener("message", async (event) => {







        try {







          const message = JSON.parse(event.data);







          await this.handleMessage(message);







        } catch (error) {







          this.log(`处理信令消息失败: ${formatError(error)}`);







        }







      });















      socket.addEventListener("close", () => {







        this.log("信令 WebSocket 已关闭");







        this.cleanupPeer();







        this.socket = null;







        this.sessionId = "";







        this.setStatus("信令连接已关闭");







        this.setConnectedState(false);







      });















      socket.addEventListener("error", () => {







        this.log("信令 WebSocket 出现错误");







        this.setStatus("信令连接失败");







      });







    }















    disconnect(reason = "browser_controller_closed") {







      if (this.socket && this.sessionId) {







        this.send({







          type: "close_session",







          sessionId: this.sessionId,







          reason







        });







      }















      if (this.socket) {







        this.socket.close();







      }















      this.cleanupPeer();







      this.sessionId = "";







      this.setStatus("会话已断开");







      this.setConnectedState(false);







    }















    send(message) {







      if (!this.socket || this.socket.readyState !== WebSocket.OPEN) {







        this.log("信令尚未连接，发送被忽略");







        return;







      }















      this.socket.send(JSON.stringify(message));







    }















    async handleMessage(message) {







      this.log(`收到信令: ${JSON.stringify(message)}`);







      switch (message.type) {







        case "hello":







          this.send({







            type: "register_controller",







            userId: this.view.userId.value.trim() || "user-web-1"







          });







          this.setStatus("已连接，正在注册浏览器控制端");







          return;







        case "registered":







          this.send({







            type: "list_devices"







          });







          this.setStatus("已注册，正在获取在线设备列表");







          return;







        case "device_list":







          this.handleDeviceList(Array.isArray(message.devices) ? message.devices : []);







          return;







        case "session_created":







          this.sessionId = message.sessionId || "";







          this.setStatus(`会话已创建: ${this.sessionId}`);







          return;







        case "session_accepted":







          this.sessionId = message.sessionId || this.sessionId;







          await this.ensurePeer();







          this.setStatus(`会话已接受: ${this.sessionId}`);







          return;







        case "signal":







          await this.handleSignal(message.payload || {});







          return;







        case "session_closed":







        case "session_failed":







        case "session_rejected":







          this.setStatus(`会话结束: ${message.reason || message.type}`);







          this.cleanupPeer();







          this.sessionId = "";







          this.setConnectedState(false);







          return;







        case "error":







          this.setStatus(`服务端错误: ${message.message || message.code || "未知错误"}`);







          return;







        default:







          return;







      }







    }















    handleDeviceList(devices) {







      const onlineDevices = devices.filter((device) => device && typeof device.deviceId === "string" && device.deviceId.length > 0);







      const onlineIds = onlineDevices.map((device) => device.deviceId);







      const onlineCount = onlineDevices.length;







      this.log(`当前已成功注册并在线的主机设备数: ${onlineCount}${onlineCount > 0 ? `，设备: ${onlineIds.join(", ")}` : ""}`);















      let targetDeviceId = this.view.targetDeviceId.value.trim();







      if (!targetDeviceId) {







        if (onlineCount === 1) {







          targetDeviceId = onlineIds[0];







          this.view.targetDeviceId.value = targetDeviceId;







        } else if (onlineCount > 1) {







          this.setStatus(`当前共有 ${onlineCount} 台在线主机，请先填写目标主机设备 ID`);







          return;







        } else {







          this.setStatus("当前未填写目标主机设备 ID，且尚未发现在线主机");







          return;







        }







      }















      if (onlineCount == 0) {







        this.log(`当前未发现在线主机，将继续尝试按目标设备 ${targetDeviceId} 创建会话`);







      }















      this.send({







        type: "create_session",







        targetDeviceId







      });







      this.setStatus(`当前在线主机数: ${onlineCount}，正在连接 ${targetDeviceId}`);







    }















)RDC_JS"

R"RDC_JS(

    async attachRemoteTrack(event) {







      if (!event.track || event.track.kind !== "video") {







        return;







      }















      this.remoteVideoTrack = event.track;







      this.videoReceiver = event.receiver || null;







      this.log(



        `浏览器收到远端轨道: kind=${event.track.kind}, id=${event.track.id}, ` +



        `muted=${event.track.muted}, readyState=${event.track.readyState}, ` +



        `streams=${event.streams ? event.streams.length : 0}`



      );















      let stream = event.streams && event.streams.length > 0 ? event.streams[0] : null;







      if (!stream) {







        if (!this.remoteStream) {







          this.remoteStream = new MediaStream();







        }















        const hasTrack = this.remoteStream.getTracks().some((track) => track.id === event.track.id);







        if (!hasTrack) {







          this.remoteStream.addTrack(event.track);







        }















        stream = this.remoteStream;







        this.log("远端视频轨未携带 MediaStream，已自动创建本地流容器");







      } else {







        this.remoteStream = stream;







      }















      this.view.remoteVideo.srcObject = stream;







      this.view.videoOverlay.classList.add("hidden");







      this.setStatus("远端视频轨已到达，正在准备播放");















      try {







        await this.view.remoteVideo.play();







        this.log("浏览器已开始播放远端视频");







        this.setStatus("正在显示远端桌面画面");







      } catch (error) {







        this.log(`浏览器视频播放调用失败: ${formatError(error)}`);







        this.setStatus("视频轨已收到，但浏览器未开始播放");







      }















      event.track.addEventListener("ended", () => {







        this.log("远端视频轨已结束");







        this.setOverlay("远端视频轨已结束");







        this.view.videoOverlay.classList.remove("hidden");







      }, { once: true });















      event.track.addEventListener("mute", () => {







        this.log("远端视频轨已 mute");







      });















      event.track.addEventListener("unmute", () => {







        this.log("远端视频轨已 unmute，浏览器开始收到媒体数据");







      });







    }















    async ensurePeer() {







      if (this.peer) {







        return;







      }















      const peer = new RTCPeerConnection({







        iceServers: []







      });







      this.peer = peer;







      this.log("已创建浏览器 PeerConnection");







      this.startStatsLoop();















      peer.addTransceiver("video", { direction: "recvonly" });















      peer.onicecandidate = (event) => {







        if (!event.candidate || !this.sessionId) {







          return;







        }















        this.send({







          type: "signal",







          sessionId: this.sessionId,







          payload: {







            kind: "candidate",







            candidate: event.candidate.candidate,







            mid: event.candidate.sdpMid || ""







          }







        });







      };















      peer.onicecandidateerror = (event) => {







        this.log(`ICE candidate 收集失败: ${event.errorText || event.errorCode || "未知错误"}`);







      };















      peer.onconnectionstatechange = () => {







        const state = peer.connectionState;







        this.log(`PeerConnection 状态 -> ${state}`);







        this.setStatus(`媒体连接状态: ${state}`);







      };















      peer.oniceconnectionstatechange = () => {







        this.log(`ICE 连接状态 -> ${peer.iceConnectionState}`);







      };















      peer.onsignalingstatechange = () => {







        this.log(`信令状态 -> ${peer.signalingState}`);







      };















      peer.ontrack = (event) => {







        this.attachRemoteTrack(event).catch((error) => {







          this.log(`挂载远端视频轨失败: ${formatError(error)}`);







          this.setStatus("远端视频轨处理失败");







        });







      };















      this.controlChannel = peer.createDataChannel("control");







      this.controlChannel.onopen = () => {







        this.log("控制数据通道已打开，发送 ping");







        this.controlChannel.send(JSON.stringify({







          type: "ping",







          seq: 1,







          ts: Math.floor(Date.now() / 1000)







        }));







      };







      this.controlChannel.onclose = () => {







        this.log("控制数据通道已关闭");







      };







      this.controlChannel.onmessage = (event) => {







        this.log(`控制通道数据 <- ${event.data}`);







      };















      const offer = await peer.createOffer();







      await peer.setLocalDescription(offer);







      this.log("浏览器已生成本地 Offer，正在发送到主机端");







      this.send({







        type: "signal",







        sessionId: this.sessionId,







        payload: {







          kind: "description",







          sdp: offer.sdp,







          sdpType: offer.type







        }







      });







    }















    async handleSignal(payload) {







      if (!payload || typeof payload !== "object") {







        return;







      }















      if (!this.peer) {







        await this.ensurePeer();







      }















      if (!this.peer) {







        return;







      }















      if (payload.kind === "description" && payload.sdp && payload.sdpType) {







        this.log(`浏览器收到远端 SDP: ${payload.sdpType}`);







        await this.peer.setRemoteDescription({







          type: payload.sdpType,







          sdp: payload.sdp







        });















        if (payload.sdpType === "offer") {







          const answer = await this.peer.createAnswer();







          await this.peer.setLocalDescription(answer);







          this.log("浏览器已生成本地 Answer，正在回传");







          this.send({







            type: "signal",







            sessionId: this.sessionId,







            payload: {







              kind: "description",







              sdp: answer.sdp,







              sdpType: answer.type







            }







          });







        }







        return;







      }















      if (payload.kind === "candidate" && payload.candidate) {







        await this.peer.addIceCandidate({







          candidate: payload.candidate,







          sdpMid: payload.mid || null







        });







      }







    }















    cleanupPeer() {







      if (this.controlChannel) {







        this.controlChannel.onopen = null;







        this.controlChannel.onclose = null;







        this.controlChannel.onmessage = null;







        try {







          this.controlChannel.close();







        } catch (_) {







        }







        this.controlChannel = null;







      }















      if (this.peer) {







        this.peer.onicecandidate = null;







        this.peer.onicecandidateerror = null;







        this.peer.ontrack = null;







        this.peer.onconnectionstatechange = null;







        this.peer.oniceconnectionstatechange = null;







        this.peer.onsignalingstatechange = null;







        try {







          this.peer.close();







        } catch (_) {







        }







        this.peer = null;







      }















      this.remoteStream = null;







      this.remoteVideoTrack = null;







      this.videoReceiver = null;







      this.lastStatsLine = "";







      this.view.remoteVideo.srcObject = null;







      this.setOverlay("等待远端视频轨");







      this.view.videoOverlay.classList.remove("hidden");







    }







  }















)RDC_JS"

R"RDC_JS(

  const controller = new BrowserController(elements);







  controller.setStatus("等待连接");







  controller.log(`浏览器控制端页面脚本已加载，当前协议=${window.location.protocol || "unknown"}`);















  window.addEventListener("error", (event) => {







    controller.log(`页面脚本异常: ${event.message || "未知错误"}`);







  });















  window.addEventListener("unhandledrejection", (event) => {







    const reason = event.reason instanceof Error ? event.reason.message : String(event.reason);







    controller.log(`未处理的 Promise 异常: ${reason}`);







  });































  elements.remoteVideo.addEventListener("loadedmetadata", () => {







    controller.log(`远端视频元数据已加载: ${elements.remoteVideo.videoWidth}x${elements.remoteVideo.videoHeight}`);







  });















  elements.remoteVideo.addEventListener("playing", () => {







    controller.log("远端 video 元素已进入 playing 状态");







  });















  elements.remoteVideo.addEventListener("canplay", () => {







    controller.log("远端 video 元素已进入 canplay 状态");







  });















  elements.remoteVideo.addEventListener("waiting", () => {







    controller.log("远端 video 元素正在等待更多媒体数据");







  });















  elements.remoteVideo.addEventListener("stalled", () => {







    controller.log("远端 video 元素已 stalled");







  });















  elements.remoteVideo.addEventListener("resize", () => {







    controller.log(`远端 video 元素尺寸变化: ${elements.remoteVideo.videoWidth}x${elements.remoteVideo.videoHeight}`);







  });















  elements.remoteVideo.addEventListener("error", () => {







    controller.log("远端 video 元素报告错误");







  });







  elements.connectBtn.addEventListener("click", () => {







    controller.log("开始连接按钮已点击");







    controller.connect().catch((error) => {







      controller.log(`浏览器控制端连接失败: ${formatError(error)}`);







      controller.setStatus("连接失败");







      controller.setConnectedState(false);







    });







  });















  elements.disconnectBtn.addEventListener("click", () => {







    controller.disconnect("browser_controller_closed");







  });















  window.addEventListener("beforeunload", () => {







    controller.disconnect("browser_page_unloaded");







  });







})();)RDC_JS";



/**

 * @brief 获取浏览器控制端HTML 页面。

 */

}  // namespace



std::string_view GetBrowserControllerHtml() {



    return kBrowserControllerHtml;



}



/**

 * @brief 获取浏览器控制端脚本内容。

 * @return 返回生成的字符串结果。

 */

std::string_view GetBrowserControllerScript() {



    return kBrowserControllerScript;



}



}  // namespace rdc::controller::ui



