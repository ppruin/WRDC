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
      --bg: #f4eee3;
      --bg-deep: #ded0b7;
      --line: rgba(77, 65, 48, 0.18);
      --line-strong: rgba(255,255,255,0.12);
      --ink: #121922;
      --muted: #66707a;
      --soft: rgba(18, 25, 34, 0.62);
      --accent: #0d6a65;
      --accent-strong: #0a4549;
      --danger: #b42318;
    }
    * { box-sizing: border-box; }
    html, body {
      margin: 0;
      min-height: 100%;
      font-family: "Microsoft YaHei UI", "Segoe UI Variable Text", "PingFang SC", sans-serif;
      color: var(--ink);
      background:
        radial-gradient(circle at top left, rgba(13,106,101,0.22), transparent 30%),
        radial-gradient(circle at bottom right, rgba(180,35,24,0.12), transparent 26%),
        linear-gradient(135deg, #f8f4ed 0%, var(--bg) 52%, var(--bg-deep) 100%);
    }
    body { overflow: hidden; }
    button, input { font: inherit; }
    .app { position: relative; min-height: 100vh; }
    .screen { position: fixed; inset: 0; transition: opacity 220ms ease, transform 220ms ease; }
    .screen-auth { display: grid; place-items: center; padding: clamp(20px, 4vw, 44px); }
    .screen-viewer { background: #030507; opacity: 0; pointer-events: none; }
    .app[data-screen="auth"] .screen-auth { opacity: 1; pointer-events: auto; transform: scale(1); }
    .app[data-screen="auth"] .screen-viewer { opacity: 0; pointer-events: none; }
    .app[data-screen="viewer"] .screen-auth { opacity: 0; pointer-events: none; transform: scale(1.02); }
    .app[data-screen="viewer"] .screen-viewer { opacity: 1; pointer-events: auto; }
    .auth-panel {
      width: min(560px, 100%);
      padding: clamp(24px, 4vw, 38px);
      border-radius: 28px;
      border: 1px solid rgba(255,255,255,0.48);
      background: linear-gradient(180deg, rgba(255,252,247,0.9) 0%, rgba(249,241,228,0.76) 100%);
      box-shadow: 0 24px 80px rgba(30,37,43,0.16);
      backdrop-filter: blur(20px);
    }
    .eyebrow {
      margin: 0 0 14px;
      font-size: 12px;
      font-weight: 700;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--accent-strong);
    }
    .auth-title { margin: 0; font-size: clamp(32px, 5vw, 52px); line-height: 0.95; letter-spacing: -0.04em; }
    .auth-subtitle { margin: 16px 0 0; color: var(--soft); font-size: 15px; line-height: 1.7; }
    .auth-form { display: grid; gap: 18px; margin-top: 30px; }
    .field-grid, .field { display: grid; gap: 14px; }
    .field { gap: 8px; }
    .field label { font-size: 13px; color: var(--muted); }
    .field input {
      width: 100%;
      padding: 14px 16px;
      border-radius: 16px;
      border: 1px solid var(--line);
      background: rgba(255,255,255,0.78);
      color: var(--ink);
      outline: none;
      transition: border-color 120ms ease, box-shadow 120ms ease, background 120ms ease;
    }
    .field input:focus {
      border-color: rgba(13,106,101,0.48);
      box-shadow: 0 0 0 4px rgba(13,106,101,0.12);
      background: #fff;
    }
    .status {
      min-height: 52px;
      padding: 14px 16px;
      border-radius: 16px;
      background: rgba(13,106,101,0.09);
      color: var(--accent-strong);
      font-size: 13px;
      line-height: 1.55;
    }
    button {
      border: 0;
      border-radius: 16px;
      padding: 14px 18px;
      cursor: pointer;
      font-size: 14px;
      font-weight: 700;
      transition: transform 120ms ease, opacity 120ms ease, background 120ms ease, border-color 120ms ease;
    }
    button:hover { transform: translateY(-1px); }
    button:active { transform: translateY(0); }
    button.primary {
      background: linear-gradient(135deg, var(--accent), var(--accent-strong));
      color: #fff;
      box-shadow: 0 18px 30px rgba(10,69,73,0.22);
    }
    button.ghost { background: rgba(255,255,255,0.08); color: #f6f7fa; border: 1px solid rgba(255,255,255,0.12); }
    button.danger { background: rgba(180,35,24,0.18); color: #ffe0db; border: 1px solid rgba(255,255,255,0.1); }
    button.full { width: 100%; }
    button:disabled { opacity: 0.55; cursor: default; transform: none; }
    .viewer-shell {
      position: relative;
      width: 100vw;
      height: 100vh;
      overflow: hidden;
      background:
        radial-gradient(circle at center, rgba(15,24,32,0.42), transparent 40%),
        linear-gradient(180deg, #071017 0%, #020409 100%);
    }
    .viewer-shell::after {
      content: "";
      position: absolute;
      inset: 0;
      background:
        radial-gradient(circle at top, rgba(13,106,101,0.22), transparent 38%),
        linear-gradient(180deg, rgba(4,8,12,0.04) 0%, rgba(4,8,12,0.55) 100%);
      opacity: 1;
      transition: opacity 180ms ease;
      pointer-events: none;
    }
    .viewer-shell[data-live="true"]::after { opacity: 0.18; }
    video {
      width: 100%;
      height: 100%;
      display: block;
      object-fit: contain;
      background: #000;
      cursor: crosshair;
      outline: none;
    }
    .viewer-controls {
      position: absolute;
      inset: 0;
      display: grid;
      place-items: center;
      padding: 24px;
      opacity: 0;
      pointer-events: none;
      transition: opacity 160ms ease;
    }
    .viewer-controls.open {
      opacity: 1;
      pointer-events: auto;
    }
    .viewer-controls-backdrop {
      position: absolute;
      inset: 0;
      background: rgba(3,5,7,0.4);
      backdrop-filter: blur(14px);
    }
    .control-window {
      position: relative;
      z-index: 1;
      width: min(760px, calc(100vw - 48px));
      max-height: min(78vh, 760px);
      display: grid;
      grid-template-rows: auto auto minmax(0, 1fr);
      gap: 16px;
      padding: 24px;
      border-radius: 28px;
      border: 1px solid var(--line-strong);
      background: linear-gradient(180deg, rgba(15,24,32,0.92) 0%, rgba(10,14,20,0.84) 100%);
      color: #f6f7fa;
      box-shadow: 0 28px 70px rgba(0,0,0,0.38);
      overflow: hidden;
    }
    .control-window-head {
      display: flex;
      justify-content: space-between;
      gap: 16px;
      align-items: flex-start;
    }
    .session-label {
      display: block;
      margin-bottom: 10px;
      font-size: 12px;
      font-weight: 700;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: rgba(255,255,255,0.56);
    }
    .session-summary { display: block; font-size: clamp(18px, 3vw, 30px); line-height: 1.1; letter-spacing: -0.04em; }
    .viewer-status { margin-top: 10px; font-size: 13px; color: rgba(255,255,255,0.76); line-height: 1.5; }
    .shortcut-tip {
      font-size: 12px;
      color: rgba(255,255,255,0.62);
      line-height: 1.6;
    }
    .viewer-actions {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 10px;
    }
    .video-overlay {
      position: absolute;
      inset: 0;
      display: grid;
      place-items: center;
      padding: 32px;
      text-align: center;
      color: rgba(247,249,251,0.9);
      font-size: clamp(18px, 2.5vw, 30px);
      letter-spacing: 0.08em;
      background:
        radial-gradient(circle at center, rgba(13,106,101,0.12), transparent 34%),
        linear-gradient(180deg, rgba(3,5,7,0.18) 0%, rgba(3,5,7,0.7) 100%);
      transition: opacity 150ms ease;
      pointer-events: none;
    }
    .video-overlay.hidden { opacity: 0; }
    .log-panel {
      min-height: 0;
      display: grid;
      grid-template-rows: auto minmax(0, 1fr);
      gap: 10px;
      border-radius: 20px;
      border: 1px solid rgba(255,255,255,0.08);
      background: rgba(255,255,255,0.04);
      overflow: hidden;
    }
    .log-title {
      padding: 14px 16px 0;
      font-size: 13px;
      font-weight: 700;
      color: rgba(255,255,255,0.72);
    }
    .log {
      overflow: auto;
      min-height: 180px;
      max-height: min(34vh, 320px);
      padding: 0 16px 16px;
      white-space: pre-wrap;
      font-size: 12px;
      line-height: 1.6;
      font-family: Consolas, "Cascadia Mono", monospace;
    }
    @media (max-width: 720px) {
      .auth-panel { border-radius: 24px; padding: 24px; }
      .viewer-controls { padding: 16px; }
      .control-window {
        width: min(100%, calc(100vw - 32px));
        padding: 18px;
        max-height: min(82vh, 760px);
      }
      .control-window-head {
        flex-direction: column;
        align-items: stretch;
      }
      .viewer-actions { grid-template-columns: 1fr; }
      .log { max-height: min(32vh, 260px); }
    }
  </style>
</head>
<body>
  <div id="app" class="app" data-screen="auth">
    <section class="screen screen-auth">
      <div class="auth-panel">
        <p class="eyebrow">Remote Desktop Control</p>
        <h1 class="auth-title">登录远程会话</h1>
        <p class="auth-subtitle">先确认控制端身份、目标主机和信令地址。提交后页面立即切换到全屏观看态，一旦远端视频轨到达，桌面画面将直接铺满整个视口。</p>
        <form id="authForm" class="auth-form">
          <div class="field-grid">
            <div class="field">
              <label for="userId">控制端用户 ID</label>
              <input id="userId" autocomplete="off">
            </div>
            <div class="field">
              <label for="targetDeviceId">目标主机设备 ID</label>
              <input id="targetDeviceId" autocomplete="off">
            </div>
            <div class="field">
              <label for="signalUrl">信令地址</label>
              <input id="signalUrl" autocomplete="off">
            </div>
          </div>
          <button id="connectBtn" class="primary full" type="submit">确认登录并进入会话</button>
          <div id="authStatusBox" class="status">等待连接</div>
        </form>
      </div>
    </section>
    <section class="screen screen-viewer">
      <div id="viewerShell" class="viewer-shell" data-live="false">
        <video id="remoteVideo" autoplay playsinline tabindex="0" aria-label="远端桌面视频"></video>
        <div id="videoOverlay" class="video-overlay" aria-hidden="true"></div>
        <div id="viewerControls" class="viewer-controls" aria-hidden="true">
          <div id="viewerControlsBackdrop" class="viewer-controls-backdrop"></div>
          <section class="control-window" role="dialog" aria-modal="false" aria-label="会话控制窗口">
            <div class="control-window-head">
              <div>
                <span class="session-label">会话控制窗口</span>
                <strong id="sessionSummary" class="session-summary">未连接</strong>
                <div id="viewerStatusBox" class="viewer-status">等待远端桌面</div>
              </div>
              <button id="hideControlsBtn" class="ghost" type="button">关闭窗口</button>
            </div>
            <div class="shortcut-tip">按 Ctrl + F2 可打开或关闭这个窗口。</div>
            <div class="viewer-actions">
              <button id="backBtn" class="ghost" type="button">返回登录</button>
              <button id="disconnectBtn" class="danger" type="button" disabled>断开会话</button>
              <button id="clearLogBtn" class="ghost" type="button">清空日志</button>
            </div>
            <div class="log-panel">
              <div class="log-title">会话日志</div>
              <div id="logBox" class="log"></div>
            </div>
          </section>
        </div>
      </div>
    </section>
  </div>
  <script src="/controller.js?v=20260401-4"></script>
</body>
</html>)RDC_HTML";
constexpr std::string_view kBrowserControllerScript = R"RDC_JS((() => {
  const $ = (id) => document.getElementById(id);
  const formatError = (error) => error instanceof Error ? error.message : String(error);

  const elements = {
    app: $("app"),
    authForm: $("authForm"),
    userId: $("userId"),
    targetDeviceId: $("targetDeviceId"),
    signalUrl: $("signalUrl"),
    connectBtn: $("connectBtn"),
    backBtn: $("backBtn"),
    disconnectBtn: $("disconnectBtn"),
    authStatusBox: $("authStatusBox"),
    viewerStatusBox: $("viewerStatusBox"),
    sessionSummary: $("sessionSummary"),
    viewerControls: $("viewerControls"),
    viewerControlsBackdrop: $("viewerControlsBackdrop"),
    hideControlsBtn: $("hideControlsBtn"),
    clearLogBtn: $("clearLogBtn"),
    logBox: $("logBox"),
    viewerShell: $("viewerShell"),
    remoteVideo: $("remoteVideo"),
    videoOverlay: $("videoOverlay")
  };

  const wsScheme = window.location.protocol === "https:" ? "wss" : "ws";
  const defaultSignalUrl = `${wsScheme}://${window.location.host}/signal`;
  const query = new URLSearchParams(window.location.search);
  const preferRealtimeControl = query.get("controlRt") === "1";
  const preferDataChannelControl = query.get("dcControl") === "1";

  if (query.has("signal")) {
    elements.signalUrl.value = query.get("signal") || "";
  }
  if (query.has("user")) {
    elements.userId.value = query.get("user") || "";
  }
  if (query.has("target")) {
    elements.targetDeviceId.value = query.get("target") || "";
  }
  elements.remoteVideo.muted = true;
  elements.remoteVideo.autoplay = true;
  elements.remoteVideo.playsInline = true;

  /**
   * @brief 封装浏览器控制端的页面状态、信令交互与远端输入同步逻辑。
   */
  class BrowserController {
    constructor(view) {
      this.view = view;
      this.socket = null;
      this.peer = null;
      this.controlChannel = null;
      this.realtimeControlChannel = null;
      this.sessionId = "";
      this.remoteStream = null;
      this.remoteVideoTrack = null;
      this.videoReceiver = null;
      this.connected = false;
      this.statsTimer = null;
      this.lastStatsLine = "";
      this.pendingReturnToAuth = false;
      this.pendingCloseStatus = "";
      this.videoHasFocus = false;
      this.remoteInputActive = false;
      this.pendingMouseSync = null;
      this.mouseSyncFrameRequested = false;
      this.lastMouseSyncSignature = "";
      this.preferRealtimeControl = preferRealtimeControl;
      this.preferDataChannelControl = preferDataChannelControl;
      this.pressedMouseButtons = new Set();
      this.pressedKeys = new Set();
      this.controlChannelVerified = false;
      this.realtimeControlChannelVerified = false;
      this.loggedControlFallbackLabels = new Set();
    }

    log(message) {
      const now = new Date().toLocaleTimeString();
      this.view.logBox.textContent += `[${now}] ${message}\n`;
      this.view.logBox.scrollTop = this.view.logBox.scrollHeight;
    }

    setStatus(message) {
      this.view.authStatusBox.textContent = message;
      this.view.viewerStatusBox.textContent = message;
    }

    setOverlay(message) {
      this.view.videoOverlay.setAttribute("aria-label", message);
    }

    setScreen(screen) {
      this.view.app.dataset.screen = screen;
      if (screen !== "viewer") {
        this.setRemoteInputActive(false, "已离开远端桌面，停止同步输入");
        this.setControlsOpen(false);
      }
    }

    setVideoLive(isLive) {
      this.view.viewerShell.dataset.live = isLive ? "true" : "false";
    }

    setControlsOpen(isOpen) {
      this.view.viewerControls.classList.toggle("open", isOpen);
      this.view.viewerControls.setAttribute("aria-hidden", isOpen ? "false" : "true");
      if (isOpen) {
        this.setRemoteInputActive(false, "会话控制窗口已打开，暂停远端输入", {
          blurVideo: true
        });
      }
    }

    toggleControls() {
      if (this.view.app.dataset.screen !== "viewer") {
        return;
      }

      const isOpen = this.view.viewerControls.classList.contains("open");
      this.setControlsOpen(!isOpen);
    }

    clearLog() {
      this.view.logBox.textContent = "";
    }

    /**
     * @brief 判断当前是否可以通过信令通道发送控制消息。
     * @returns {boolean} 返回是否具备信令控制转发条件。
     */
    canSendControlOverSignal() {
      return this.socket !== null &&
        this.socket.readyState === WebSocket.OPEN &&
        typeof this.sessionId === "string" &&
        this.sessionId.length > 0;
    }

    /**
     * @brief 判断指定数据通道是否已打开。
     * @param channel 数据通道对象。
     * @returns {boolean} 返回是否成功或条件是否满足。
     */
    isDataChannelOpen(channel) {
      return channel !== null && channel.readyState === "open";
    }
)RDC_JS"
R"RDC_JS(

    /**
     * @brief 判断指定逻辑控制通道是否已经通过回包验证。
     * @param label 逻辑通道标签。
     * @returns {boolean} 返回是否成功或条件是否满足。
     */
    isControlChannelVerified(label) {
      if (label === "control_rt") {
        return this.realtimeControlChannelVerified;
      }

      return this.controlChannelVerified;
    }

    /**
     * @brief 更新指定逻辑控制通道的验证状态。
     * @param label 逻辑通道标签。
     * @param verified 是否已通过验证。
     */
    setControlChannelVerified(label, verified) {
      if (label === "control_rt") {
        this.realtimeControlChannelVerified = verified;
      } else {
        this.controlChannelVerified = verified;
      }

      if (verified) {
        this.loggedControlFallbackLabels.delete(label);
      }
    }

    /**
     * @brief 判断控制负载是否应优先走实时控制通道。
     * @param payload 待发送的控制消息对象。
     * @returns {boolean} 返回是否成功或条件是否满足。
     */
    isRealtimeControlPayload(payload) {
      const type = payload && typeof payload.type === "string" ? payload.type : "";
      return this.preferRealtimeControl && (type === "mouse_move" || type === "mouse_wheel");
    }

    /**
     * @brief 获取控制负载对应的逻辑通道标签。
     * @param payload 待发送的控制消息对象。
     * @returns {string} 返回逻辑通道标签。
     */
    getPreferredControlChannelLabel(payload) {
      return this.isRealtimeControlPayload(payload) ? "control_rt" : "control";
    }

    /**
     * @brief 获取优先用于发送控制负载的数据通道。
     * @param payload 待发送的控制消息对象。
     * @returns {RTCDataChannel|null} 返回可用数据通道对象。
     */
    getPreferredDataChannel(payload, options = {}) {
      const allowUnverified = options.allowUnverified === true;

      if (this.isRealtimeControlPayload(payload)) {
        if (this.isDataChannelOpen(this.realtimeControlChannel) &&
            (allowUnverified || this.isControlChannelVerified("control_rt"))) {
          return this.realtimeControlChannel;
        }
        if (this.isDataChannelOpen(this.controlChannel) &&
            (allowUnverified || this.isControlChannelVerified("control"))) {
          return this.controlChannel;
        }
      } else {
        if (this.isDataChannelOpen(this.controlChannel) &&
            (allowUnverified || this.isControlChannelVerified("control"))) {
          return this.controlChannel;
        }
        if (this.isDataChannelOpen(this.realtimeControlChannel) &&
            (allowUnverified || this.isControlChannelVerified("control_rt"))) {
          return this.realtimeControlChannel;
        }
      }

      return null;
    }

    /**
     * @brief 判断当前是否具备任一控制消息发送通道。
     * @returns {boolean} 返回是否存在可用控制传输。
     */
    canSendControlTransport() {
      return this.canSendControlOverSignal() ||
        this.isDataChannelOpen(this.controlChannel) ||
        this.isDataChannelOpen(this.realtimeControlChannel);
    }

    /**
     * @brief 通过信令 WebSocket 转发控制负载。
     * @param payload 待发送的控制消息对象。
     * @param logErrorPrefix 发送失败时的日志前缀。
     * @returns {boolean} 返回是否成功发送。
     */
    sendSignalControlPayload(payload, logErrorPrefix = "信令控制消息发送失败") {
      if (!this.canSendControlOverSignal()) {
        return false;
      }

      try {
        this.socket.send(JSON.stringify({
            type: "signal",
            sessionId: this.sessionId,
            payload: {
              kind: "control",
              channel: this.getPreferredControlChannelLabel(payload),
              data: payload
            }
          }));
        if (payload && typeof payload.type === "string" && payload.type !== "mouse_move") {
          this.log(`控制消息通过信令发送: type=${payload.type}, channel=${this.getPreferredControlChannelLabel(payload)}`);
        }
        return true;
      } catch (error) {
        this.log(`${logErrorPrefix}: ${formatError(error)}`);
        return false;
      }
    }

    /**
     * @brief 通过 WebRTC 控制数据通道发送控制负载。
     * @param payload 待发送的控制消息对象。
     * @param logErrorPrefix 发送失败时的日志前缀。
     * @returns {boolean} 返回是否成功发送。
     */
    sendDataChannelControlPayload(payload, logErrorPrefix = "控制通道发送失败", options = {}) {
      const channel = this.getPreferredDataChannel(payload, options);
      if (channel === null) {
        return false;
      }

      try {
        channel.send(JSON.stringify(payload));
        if (payload && typeof payload.type === "string" && payload.type !== "mouse_move") {
          const channelLabel = channel === this.realtimeControlChannel ? "control_rt" : "control";
          this.log(`控制消息通过数据通道发送: type=${payload.type}, channel=${channelLabel}`);
        }
        return true;
      } catch (error) {
        this.log(`${logErrorPrefix}: ${formatError(error)}`);
        return false;
      }
    }

    /**
     * @brief 附加控制数据通道的默认事件处理逻辑。
     * @param channel 数据通道对象。
     * @param label 逻辑通道标签。
     * @param options 附加行为选项。
     */
    attachControlChannel(channel, label, options = {}) {
      if (!channel) {
        return;
      }

      const friendlyLabel = label === "control_rt" ? "实时控制" : "控制";
      channel.onopen = () => {
        this.setControlChannelVerified(label, false);
        this.log(`${friendlyLabel}数据通道已打开`);
        if (options.sendPing === true) {
          const pingPayload = {
            type: "ping",
            seq: 1,
            ts: Math.floor(Date.now() / 1000)
          };

          if (this.sendDataChannelControlPayload(
            pingPayload,
            `${friendlyLabel}数据通道探测发送失败`,
            { allowUnverified: true }
          )) {
            this.log(`${friendlyLabel}数据通道已发送直连探测 ping`);
          }

          if (this.sendSignalControlPayload(pingPayload, `${friendlyLabel}信令探测发送失败`)) {
            this.log(`${friendlyLabel}数据通道待验证，已通过信令补发 ping`);
          }
        }
      };
      channel.onclose = () => {
        this.setControlChannelVerified(label, false);
        this.log(`${friendlyLabel}数据通道已关闭`);
        if (!this.canSendControlTransport()) {
          this.setRemoteInputActive(false, `${friendlyLabel}数据通道已关闭，停止远端输入`);
        }
      };
      channel.onmessage = (event) => {
        if (!this.isControlChannelVerified(label)) {
          this.setControlChannelVerified(label, true);
          this.log(`${friendlyLabel}数据通道已通过回包验证，后续控制将优先走数据通道`);
        }
        this.log(`${friendlyLabel}通道数据 <- ${event.data}`);
      };
    }

    /**
     * @brief 发送控制负载，优先走 RTC 数据通道，信令转发作为备用。
     * @param payload 待发送的控制消息对象。
     * @param logErrorPrefix 发送失败时的日志前缀。
     * @returns {boolean} 返回是否成功发送。
     */
    sendControlPayload(payload, logErrorPrefix = "控制消息发送失败") {
      if (this.preferDataChannelControl) {
        if (this.sendDataChannelControlPayload(payload, logErrorPrefix)) {
          return true;
        }

        const preferredLabel = this.getPreferredControlChannelLabel(payload);
        const preferredChannel = preferredLabel === "control_rt"
          ? this.realtimeControlChannel
          : this.controlChannel;

        if (this.isDataChannelOpen(preferredChannel) &&
            !this.isControlChannelVerified(preferredLabel) &&
            !this.loggedControlFallbackLabels.has(preferredLabel)) {
          this.loggedControlFallbackLabels.add(preferredLabel);
          this.log(`${preferredLabel} 数据通道尚未验证，当前控制消息先回退到信令通道`);
        }
      }

      if (this.sendSignalControlPayload(payload, logErrorPrefix)) {
        return true;
      }

      return this.sendDataChannelControlPayload(payload, logErrorPrefix, { allowUnverified: true });
    }

    /**
     * @brief 主动释放当前仍然保持按下状态的远端键鼠输入。
     */
    releaseAllRemoteInputs() {
      if (this.pressedMouseButtons.size > 0) {
        Array.from(this.pressedMouseButtons).forEach((button) => {
          this.sendControlPayload({
            type: "mouse_button",
            button,
            pressed: false,
            ts: Date.now()
          }, "控制通道发送鼠标释放失败");
        });
        this.pressedMouseButtons.clear();
      }

      if (this.pressedKeys.size > 0) {
        Array.from(this.pressedKeys).forEach((code) => {
          this.sendControlPayload({
            type: "key_up",
            code,
            ts: Date.now()
          }, "控制通道发送按键释放失败");
        });
        this.pressedKeys.clear();
      }
    }

    /**
     * @brief 更新远端输入捕获激活状态。
     * @param isActive 是否启用远端输入同步。
     * @param reason 状态切换时写入日志的原因说明。
     * @param options 额外控制选项。
     */
    setRemoteInputActive(isActive, reason = "", options = {}) {
      const blurVideo = options && options.blurVideo === true;
      const focusVideo = options && options.focusVideo === true;
      if (this.remoteInputActive === isActive) {
        if (focusVideo) {
          this.view.remoteVideo.focus({ preventScroll: true });
        }
        if (blurVideo) {
          this.view.remoteVideo.blur();
        }
        return;
      }

      if (!isActive) {
        this.releaseAllRemoteInputs();
      }

      this.remoteInputActive = isActive;
      this.pendingMouseSync = null;
      this.lastMouseSyncSignature = "";

      if (focusVideo) {
        this.view.remoteVideo.focus({ preventScroll: true });
      }
      if (blurVideo) {
        this.view.remoteVideo.blur();
      }

      if (reason) {
        this.log(reason);
      }
    }

    /**
     * @brief 更新远端视频是否获得焦点的状态。
     * @param hasFocus 当前视频元素是否拥有焦点。
     */
    setVideoFocusState(hasFocus) {
      if (this.videoHasFocus === hasFocus) {
        return;
      }

      this.videoHasFocus = hasFocus;
      if (hasFocus) {
        this.setRemoteInputActive(true, "远端视频已获得焦点，开始同步键鼠输入");
      } else {
        this.pendingMouseSync = null;
        this.lastMouseSyncSignature = "";
        this.log("远端视频已失去焦点");
      }
    }

    /**
     * @brief 判断当前是否满足鼠标位置同步条件。
     * @returns {boolean} 返回是否允许发送鼠标位置。
     */
    canSendMouseSync() {
      return this.view.app.dataset.screen === "viewer" &&
        this.remoteInputActive &&
        this.canSendControlTransport() &&
        this.view.remoteVideo.videoWidth > 0 &&
        this.view.remoteVideo.videoHeight > 0;
    }

    /**
     * @brief 判断当前是否满足输入事件同步条件。
     * @returns {boolean} 返回是否允许发送键鼠输入。
     */
    canSendInputEvents() {
      return this.view.app.dataset.screen === "viewer" &&
        this.remoteInputActive &&
        this.canSendControlTransport();
    }

    /**
     * @brief 计算鼠标在实际视频画面内的归一化坐标。
     * @param event 浏览器指针事件对象。
     * @returns {{normalizedX:number, normalizedY:number}|null} 返回归一化结果；若命中黑边或画面无效则返回空值。
     */
    getNormalizedMousePosition(event) {
      const video = this.view.remoteVideo;
      const rect = video.getBoundingClientRect();
      if (rect.width <= 0 || rect.height <= 0 || video.videoWidth <= 0 || video.videoHeight <= 0) {
        return null;
      }

      const videoAspect = video.videoWidth / video.videoHeight;
      const boxAspect = rect.width / rect.height;
      let renderedWidth = rect.width;
      let renderedHeight = rect.height;
      let offsetX = 0;
      let offsetY = 0;

      if (boxAspect > videoAspect) {
        renderedWidth = rect.height * videoAspect;
        offsetX = (rect.width - renderedWidth) / 2;
      } else {
        renderedHeight = rect.width / videoAspect;
        offsetY = (rect.height - renderedHeight) / 2;
      }

      const localX = event.clientX - rect.left - offsetX;
      const localY = event.clientY - rect.top - offsetY;
      if (localX < 0 || localY < 0 || localX > renderedWidth || localY > renderedHeight) {
        return null;
      }

      return {
        normalizedX: Math.min(Math.max(localX / renderedWidth, 0), 1),
        normalizedY: Math.min(Math.max(localY / renderedHeight, 0), 1)
      };
    }

    /**
     * @brief 将鼠标位置同步请求合并到下一帧发送。
     * @param position 归一化鼠标坐标。
     */
    queueMouseSync(position) {
      if (!position) {
        return;
      }

      this.pendingMouseSync = position;
      if (this.mouseSyncFrameRequested) {
        return;
      }

      this.mouseSyncFrameRequested = true;
      window.requestAnimationFrame(() => {
        this.mouseSyncFrameRequested = false;
        this.flushMouseSync();
      });
    }

    /**
     * @brief 发送当前待处理的鼠标位置同步消息。
     */
    flushMouseSync() {
      if (!this.pendingMouseSync || !this.canSendMouseSync()) {
        return;
      }

      const { normalizedX, normalizedY } = this.pendingMouseSync;
      this.pendingMouseSync = null;

      const signature = `${normalizedX.toFixed(4)}:${normalizedY.toFixed(4)}`;
      if (signature === this.lastMouseSyncSignature) {
        return;
      }

      if (this.sendControlPayload({
        type: "mouse_move",
        normalizedX,
        normalizedY,
        ts: Date.now()
      }, "控制通道发送鼠标位置失败")) {
        this.lastMouseSyncSignature = signature;
      }
    }

    /**
     * @brief 发送鼠标按键状态到远端主机。
     * @param button 浏览器鼠标按键编号。
     * @param pressed 是否处于按下状态。
     * @param position 可选的归一化坐标，用于在点击前先同步鼠标位置。
     */
    sendMouseButton(button, pressed, position = null) {
      if (!this.canSendInputEvents()) {
        return;
      }

      const payload = {
        type: "mouse_button",
        button,
        pressed,
        ts: Date.now()
      };

      if (position) {
        payload.normalizedX = position.normalizedX;
        payload.normalizedY = position.normalizedY;
      }

      if (this.sendControlPayload(payload, "控制通道发送鼠标按键失败")) {
        if (pressed) {
          this.pressedMouseButtons.add(button);
        } else {
          this.pressedMouseButtons.delete(button);
        }
      }
    }
)RDC_JS"
R"RDC_JS(
    /**
     * @brief 将浏览器滚轮增量转换为 Windows 风格滚轮步进值。
     * @param event 浏览器滚轮事件对象。
     * @returns {{deltaX:number, deltaY:number}} 返回水平与垂直滚轮增量。
     */
    normalizeWheelDelta(event) {
      const scale = event.deltaMode === WheelEvent.DOM_DELTA_LINE
        ? 1
        : event.deltaMode === WheelEvent.DOM_DELTA_PAGE
          ? 3
          : 1 / 100;
      const convert = (delta) => {
        if (!Number.isFinite(delta) || delta === 0) {
          return 0;
        }

        let wheelDelta = Math.round(delta * scale * 120);
        if (wheelDelta === 0) {
          wheelDelta = delta > 0 ? 120 : -120;
        }

        return wheelDelta;
      };

      return {
        deltaX: convert(event.deltaX),
        deltaY: -convert(event.deltaY)
      };
    }

    /**
     * @brief 发送滚轮输入到远端主机。
     * @param event 浏览器滚轮事件对象。
     */
    sendWheel(event) {
      if (!this.canSendInputEvents()) {
        return;
      }

      const wheel = this.normalizeWheelDelta(event);
      if (wheel.deltaX === 0 && wheel.deltaY === 0) {
        return;
      }

      const payload = {
        type: "mouse_wheel",
        deltaX: wheel.deltaX,
        deltaY: wheel.deltaY,
        ts: Date.now()
      };

      const position = this.getNormalizedMousePosition(event);
      if (position) {
        payload.normalizedX = position.normalizedX;
        payload.normalizedY = position.normalizedY;
      }

      this.sendControlPayload(payload, "控制通道发送滚轮失败");
    }

    /**
     * @brief 发送键盘按下或抬起事件到远端主机。
     * @param event 浏览器键盘事件对象。
     * @param pressed 是否为按下事件。
     * @returns {boolean} 返回是否成功发送。
     */
    sendKeyboardEvent(event, pressed) {
      if (!this.canSendInputEvents() || event.isComposing) {
        return false;
      }

      const code = typeof event.code === "string" ? event.code : "";
      if (!code) {
        return false;
      }

      const payload = {
        type: pressed ? "key_down" : "key_up",
        code,
        key: typeof event.key === "string" ? event.key : "",
        location: Number.isFinite(event.location) ? event.location : 0,
        repeat: Boolean(event.repeat),
        ts: Date.now()
      };

      const sent = this.sendControlPayload(payload, "控制通道发送按键失败");
      if (!sent) {
        return false;
      }

      if (pressed) {
        this.pressedKeys.add(code);
      } else {
        this.pressedKeys.delete(code);
      }

      return true;
    }

    updateSessionSummary() {
      const userId = this.view.userId.value.trim() || "user-web-1";
      const targetDeviceId = this.view.targetDeviceId.value.trim() || "待选择主机";
      const signalText = this.view.signalUrl.value.trim() || defaultSignalUrl;
      let endpoint = signalText;

      try {
        endpoint = new URL(signalText).host || signalText;
      } catch (_) {
      }

      const sessionSuffix = this.sessionId ? ` · 会话 ${this.sessionId}` : "";
      this.view.sessionSummary.textContent = `${userId} -> ${targetDeviceId} · ${endpoint}${sessionSuffix}`;
    }

    setConnectedState(isConnected) {
      this.connected = isConnected;
      this.view.connectBtn.disabled = isConnected;
      this.view.disconnectBtn.disabled = !isConnected;
    }

    handleSocketClosed(statusMessage, returnToAuth) {
      this.cleanupPeer();
      this.socket = null;
      this.sessionId = "";
      this.setConnectedState(false);
      this.setStatus(statusMessage);
      this.updateSessionSummary();
      if (returnToAuth) {
        this.setScreen("auth");
      }
    }

    closeSocket(returnToAuth, statusMessage) {
      this.pendingReturnToAuth = returnToAuth;
      this.pendingCloseStatus = statusMessage;

      if (this.socket &&
          (this.socket.readyState === WebSocket.OPEN || this.socket.readyState === WebSocket.CONNECTING)) {
        this.socket.close();
        return;
      }

      this.handleSocketClosed(statusMessage, returnToAuth);
      this.pendingReturnToAuth = false;
      this.pendingCloseStatus = "";
    }

    startStatsLoop() {
      this.stopStatsLoop();
      this.statsTimer = window.setInterval(async () => {
        if (!this.peer) {
          return;
        }

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

            if (report.type === "inbound-rtp" &&
                !report.isRemote &&
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
)RDC_JS"
R"RDC_JS(
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

      const signalUrl = this.view.signalUrl.value.trim();
      if (!signalUrl) {
        this.setStatus("请输入信令地址");
        this.log("未填写信令地址，无法建立浏览器会话");
        return;
      }

      this.pendingReturnToAuth = false;
      this.pendingCloseStatus = "";
      this.updateSessionSummary();
      this.setScreen("viewer");
      this.setVideoLive(false);
      this.setControlsOpen(false);
      this.setOverlay("正在连接远端会话");
      this.view.videoOverlay.classList.remove("hidden");
      this.setStatus("正在建立浏览器控制会话");
      this.log(`连接信令服务: ${signalUrl}`);
      this.setConnectedState(true);

      const socket = new WebSocket(signalUrl);
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
        const returnToAuth = this.pendingReturnToAuth;
        const statusMessage = this.pendingCloseStatus || "信令连接已关闭";
        this.pendingReturnToAuth = false;
        this.pendingCloseStatus = "";
        this.log("信令 WebSocket 已关闭");
        this.handleSocketClosed(statusMessage, returnToAuth);
      });

      socket.addEventListener("error", () => {
        this.log("信令 WebSocket 出现错误");
        this.setStatus("信令连接失败");
      });
    }

    disconnect(reason = "browser_controller_closed", options = {}) {
      const { returnToAuth = true, statusMessage = "会话已断开" } = options;

      if (this.socket && this.sessionId && this.socket.readyState === WebSocket.OPEN) {
        this.send({
          type: "close_session",
          sessionId: this.sessionId,
          reason
        });
      }

      this.closeSocket(returnToAuth, statusMessage);
    }

    returnToAuth(statusMessage = "等待连接") {
      this.closeSocket(true, statusMessage);
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
          this.send({ type: "list_devices" });
          this.setStatus("已注册，正在获取在线设备列表");
          return;

        case "device_list":
          this.handleDeviceList(Array.isArray(message.devices) ? message.devices : []);
          return;

        case "session_created":
          this.sessionId = message.sessionId || "";
          this.updateSessionSummary();
          this.setStatus(`会话已创建: ${this.sessionId}`);
          return;

        case "session_accepted":
          this.sessionId = message.sessionId || this.sessionId;
          this.updateSessionSummary();
          await this.ensurePeer();
          this.setStatus(`会话已接受: ${this.sessionId}`);
          this.setOverlay("正在等待远端视频轨");
          return;

        case "signal":
          await this.handleSignal(message.payload || {});
          return;

        case "session_closed":
        case "session_failed":
        case "session_rejected":
          this.closeSocket(true, `会话结束: ${message.reason || message.type}`);
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

      this.log(
        `当前已成功注册并在线的主机设备数: ${onlineCount}` +
        `${onlineCount > 0 ? `，设备: ${onlineIds.join(", ")}` : ""}`
      );

      let targetDeviceId = this.view.targetDeviceId.value.trim();
      if (!targetDeviceId) {
        if (onlineCount === 1) {
          targetDeviceId = onlineIds[0];
          this.view.targetDeviceId.value = targetDeviceId;
          this.updateSessionSummary();
        } else if (onlineCount > 1) {
          this.returnToAuth(`当前共有 ${onlineCount} 台在线主机，请先填写目标主机设备 ID`);
          return;
        } else {
          this.returnToAuth("当前未填写目标主机设备 ID，且尚未发现在线主机");
          return;
        }
      }

      if (onlineCount === 0) {
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
      this.setVideoLive(true);
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
        this.setVideoLive(false);
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

      const peer = new RTCPeerConnection({ iceServers: [] });
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
      this.attachControlChannel(this.controlChannel, "control", {
        sendPing: true
      });

      if (this.preferRealtimeControl) {
        try {
          this.realtimeControlChannel = peer.createDataChannel("control_rt", {
            ordered: false,
            maxRetransmits: 0
          });
          this.attachControlChannel(this.realtimeControlChannel, "control_rt");
        } catch (error) {
          this.realtimeControlChannel = null;
          this.log(`创建实时控制数据通道失败: ${formatError(error)}`);
        }
      }

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
      this.stopStatsLoop();

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

      if (this.realtimeControlChannel) {
        this.realtimeControlChannel.onopen = null;
        this.realtimeControlChannel.onclose = null;
        this.realtimeControlChannel.onmessage = null;
        try {
          this.realtimeControlChannel.close();
        } catch (_) {
        }
        this.realtimeControlChannel = null;
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
      this.setRemoteInputActive(false);
      this.mouseSyncFrameRequested = false;
      this.videoHasFocus = false;
      this.view.remoteVideo.srcObject = null;
      this.view.remoteVideo.blur();
      this.setVideoLive(false);
      this.setOverlay("正在等待远端视频轨");
      this.view.videoOverlay.classList.remove("hidden");
    }
  }
)RDC_JS"
R"RDC_JS(
  const controller = new BrowserController(elements);
  controller.setScreen("auth");
  controller.setControlsOpen(false);
  controller.setStatus("等待连接");
  controller.setOverlay("正在等待远端视频轨");
  controller.updateSessionSummary();
  controller.log(`浏览器控制端页面脚本已加载，当前协议=${window.location.protocol || "unknown"}`);
  controller.log(
    preferDataChannelControl
      ? (preferRealtimeControl
          ? "已显式启用数据通道控制实验路径，mouse_move/mouse_wheel 将优先尝试 control_rt"
          : "已显式启用数据通道控制实验路径，默认优先尝试 control")
      : "默认使用信令通道发送键鼠控制；如需测试数据通道控制，请在地址后追加 ?dcControl=1，可叠加 ?controlRt=1"
  );

  window.addEventListener("error", (event) => {
    controller.log(`页面脚本异常: ${event.message || "未知错误"}`);
  });

  window.addEventListener("unhandledrejection", (event) => {
    const reason = event.reason instanceof Error ? event.reason.message : String(event.reason);
    controller.log(`未处理的 Promise 异常: ${reason}`);
  });

  [elements.userId, elements.targetDeviceId, elements.signalUrl].forEach((input) => {
    input.addEventListener("input", () => {
      controller.updateSessionSummary();
    });
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

  elements.remoteVideo.addEventListener("pointerdown", () => {
    controller.setRemoteInputActive(true, "远端输入捕获已激活", {
      focusVideo: true
    });
    controller.setVideoFocusState(true);
  });

  elements.remoteVideo.addEventListener("focus", () => {
    controller.setVideoFocusState(true);
  });

  elements.remoteVideo.addEventListener("blur", () => {
    controller.setVideoFocusState(false);
  });

  elements.remoteVideo.addEventListener("pointermove", (event) => {
    if (!controller.canSendMouseSync()) {
      return;
    }

    const position = controller.getNormalizedMousePosition(event);
    if (position) {
      controller.queueMouseSync(position);
    }
  });

  elements.remoteVideo.addEventListener("mousedown", (event) => {
    controller.setRemoteInputActive(true, "", {
      focusVideo: true
    });
    const position = controller.getNormalizedMousePosition(event);
    if (!position) {
      return;
    }

    event.preventDefault();
    controller.sendMouseButton(event.button, true, position);
  });

  window.addEventListener("mouseup", (event) => {
    if (!controller.pressedMouseButtons.has(event.button)) {
      return;
    }

    event.preventDefault();
    controller.sendMouseButton(event.button, false, controller.getNormalizedMousePosition(event));
  });

  elements.remoteVideo.addEventListener("contextmenu", (event) => {
    if (controller.remoteInputActive) {
      event.preventDefault();
    }
  });

  elements.remoteVideo.addEventListener("wheel", (event) => {
    if (!controller.canSendInputEvents()) {
      return;
    }

    event.preventDefault();
    controller.sendWheel(event);
  }, { passive: false });

  elements.authForm.addEventListener("submit", (event) => {
    event.preventDefault();
    controller.log("登录会话按钮已点击");
    controller.connect().catch((error) => {
      controller.log(`浏览器控制端连接失败: ${formatError(error)}`);
      controller.setStatus("连接失败");
      controller.setConnectedState(false);
      controller.setScreen("auth");
    });
  });

  elements.disconnectBtn.addEventListener("click", () => {
    controller.disconnect("browser_controller_closed", {
      returnToAuth: true,
      statusMessage: "会话已断开"
    });
  });

  elements.backBtn.addEventListener("click", () => {
    controller.returnToAuth("等待连接");
  });

  elements.hideControlsBtn.addEventListener("click", () => {
    controller.setControlsOpen(false);
  });

  elements.viewerControlsBackdrop.addEventListener("click", () => {
    controller.setControlsOpen(false);
  });

  elements.clearLogBtn.addEventListener("click", () => {
    controller.clearLog();
  });

  window.addEventListener("keydown", (event) => {
    if (event.ctrlKey && event.key === "F2") {
      event.preventDefault();
      controller.toggleControls();
      return;
    }

    if (event.key === "Escape" && elements.viewerControls.classList.contains("open")) {
      controller.setControlsOpen(false);
      return;
    }

    if (controller.sendKeyboardEvent(event, true)) {
      event.preventDefault();
    }
  });

  window.addEventListener("keyup", (event) => {
    if (controller.sendKeyboardEvent(event, false)) {
      event.preventDefault();
    }
  });

  window.addEventListener("blur", () => {
    controller.setRemoteInputActive(false, "浏览器窗口已失去焦点，停止远端输入");
    controller.setVideoFocusState(false);
  });

  window.addEventListener("beforeunload", () => {
    controller.disconnect("browser_page_unloaded", {
      returnToAuth: false,
      statusMessage: "页面已关闭"
    });
  });
})();)RDC_JS";

}  // namespace

std::string_view GetBrowserControllerHtml() {
    return kBrowserControllerHtml;
}

std::string_view GetBrowserControllerScript() {
    return kBrowserControllerScript;
}

}  // namespace rdc::controller::ui
