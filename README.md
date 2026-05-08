# chttp

A minimalist, high-performance C11 HTTP/1.1 server using `epoll` and `sendfile`.

## Features
- **Fast:** Non-blocking edge-triggered epoll event loop.
- **Efficient:** Zero-copy file delivery via `sendfile`.
- **Live-Reload:** Built-in `inotify` browser auto-refresh.
- **Safe:** Fuzz-tested parser and path sanitization.
- **Zero Dependencies:** Pure C11/Linux code.

## Quick Start
```bash
make release
./build_release/chttp --port 8080 --root ./public_html --live-reload
```

## Options
- `--port <port>`: Port to listen on (default: 8080)
- `--root <path>`: Web root directory (default: ./www)
- `--live-reload`: Enable auto-refresh on file changes
- `--max-conn <n>`: Max concurrent connections (default: 10000)

## License
MIT
