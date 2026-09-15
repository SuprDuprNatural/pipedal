# Debugging PiPedal

PiPedal has two main development surfaces: the C++ services and the React/Vite
web application. Run only one `pipedald` instance against a data directory at a
time. A debug server pointed at `/var/pipedal` can change the live board and
settings, so use a separate test data root when isolation matters.

## Components

- `pipedald` hosts the web application and WebSocket API and runs the audio
  engine.
- `pipedaladmind` performs the small set of operations that require root
  privileges, such as shutdown and network configuration.
- `pipedalconfig` configures and controls the installed services.
- `pipedaltest` contains the native Catch2 regression suite.
- `vite/` contains the TypeScript and React frontend.

The installed `pipedald` service runs as the unprivileged `pipedal_d` account.
To debug against the installed `/etc/pipedal` and `/var/pipedal` directories,
add the development account to the same group, then log out and back in:

```sh
sudo usermod -a -G pipedal_d "$USER"
```

## Debugging the C++ server

Stop the installed instance before starting a debugger on the same port or data
directory:

```sh
sudo systemctl stop pipedald
# Or stop both PiPedal services:
pipedalconfig --stop
```

Build a Debug or RelWithDebInfo configuration with CMake, select `pipedald` as
the debug target, and pass the server its configuration and web roots:

```sh
build/src/pipedald /etc/pipedal/config /etc/pipedal/react \
  -port 127.0.0.1:8080 -log-level debug
```

The repository's `.vscode/launch.json` contains equivalent launch settings for
Visual Studio Code with CMake Tools. Use `journalctl -u pipedald` to inspect the
installed service logs.

`pipedaladmind` is normally easier to inspect by attaching a debugger to the
running service. `pipedald` can run without it, but shutdown, reboot, audio
configuration, and Wi-Fi configuration operations will be unavailable.

## Debugging the web application

Install the locked dependencies once, then start Vite and point its proxy at a
running server:

```sh
cd vite
npm ci
PIPEDAL_SERVER=http://127.0.0.1:8080 npm run dev
```

Open <http://localhost:5173>. Vite serves the frontend and proxies `/pipedal`,
`/resources`, and `/var` requests to `PIPEDAL_SERVER`; no generated config file
needs to be edited. Vite rebuilds changed sources automatically, and browser
developer tools can debug the original TypeScript through source maps.

To use the installed service instead, set `PIPEDAL_SERVER` to its HTTP origin.
Keep the development server private unless another device genuinely needs to
connect; `vite/debug.sh` deliberately listens on all interfaces.

Before committing frontend changes, run:

```sh
npm run lint
npm run build
```

---

[The build system](TheBuildSystem.md) · [Documentation index](Documentation.md)
· [PiPedal architecture](Architecture.md)
