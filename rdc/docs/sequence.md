# Sequence

1. `agent` connects to `/signal` and sends `register_device`
2. `controller` connects to `/signal` and sends `register_controller`
3. `controller` sends `create_session`
4. `server` forwards `session_request` to `agent`
5. `agent` sends `accept_session`
6. `controller` creates a `control` data channel and sends the offer
7. `agent` applies the offer and returns answer/candidates
8. both sides enter `connected`
9. `controller` sends `ping`
10. `agent` returns `pong`
11. `controller` sends `close_session`
