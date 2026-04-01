(() => {
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
})();