# PiPedal architecture

PiPedal consists of a browser client, an unprivileged audio/server process, and
a small privileged administration service.

![PiPedal production architecture](img/Architecture.png)

## Browser client

The client is a React application written in TypeScript and built with Vite.
In production, `pipedald` serves its static files. After loading, the client
opens a WebSocket connection to the same server. Requests and replies use JSON,
and server events keep every connected client's `PiPedalModel` synchronized
with the live engine state.

## Server processes

`pipedald` is the C++ host. It serves HTTP and WebSocket traffic, discovers LV2
plugins, owns the current pedalboard and preset state, and processes audio. It
runs under the unprivileged `pipedal_d` service account.

`pipedaladmind` performs operations that require elevated privileges, including
shutdown, reboot, and selected system configuration changes. Access to its
local IPC endpoint is restricted to the `pipedal_d` group.

Production commonly uses port 80, although the installer can select another
port when it is already occupied. The server-generated `/var/config.json`
describes the active address, port, upload limit, and build mode to the client.

## Development topology

During frontend development, Vite serves the application on port 5173 and
proxies dynamic routes to a running `pipedald` instance:

![PiPedal development architecture](img/DebugArchitecture.png)

Set the backend without editing source or generated files:

```sh
cd vite
PIPEDAL_SERVER=http://127.0.0.1:8080 npm run dev
```

See [Debugging PiPedal](Debugging.md) for the complete development workflow.

---

[Debugging PiPedal](Debugging.md) · [Documentation index](Documentation.md)
