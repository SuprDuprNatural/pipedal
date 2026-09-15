# PiPedal web application

This directory contains PiPedal's React and TypeScript frontend.

## Development

Install exactly the dependencies recorded in `package-lock.json`:

```sh
npm ci
```

Start Vite and point its dynamic routes at a running `pipedald` server:

```sh
PIPEDAL_SERVER=http://127.0.0.1:8080 npm run dev
```

The application is then available at <http://localhost:5173>. Without
`PIPEDAL_SERVER`, the proxy defaults to `http://localhost:8080`.

## Validation and production build

```sh
npm run lint
npm run build
```

`npm run build` type-checks the project and writes the production bundle to
`dist/`. The repository-level CMake build invokes `build.sh`, which also makes
gzip copies of generated JavaScript assets for the production server.

See [the debugging guide](../docs/Debugging.md) for C++ server and Visual Studio
Code setup.
