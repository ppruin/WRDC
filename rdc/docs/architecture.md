# Remote Desktop Architecture

Current runnable V1 skeleton in this workspace:

- `server`: `uWebSockets` signaling service
- `agent`: Windows host client with signaling + WebRTC data channel accept side
- `controller`: Windows control client with signaling + WebRTC data channel offer side
- `protocol`: shared signaling/control helpers

Validated path:

1. host registers
2. controller creates session
3. offer/answer/candidates are exchanged through the signaling server
4. control data channel opens
5. controller sends `ping`
6. host replies `pong`
