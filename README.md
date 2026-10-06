# Atlas

[![CI](https://github.com/seansjlee/atlas-posix-server/actions/workflows/ci.yml/badge.svg)](https://github.com/seansjlee/atlas-posix-server/actions/workflows/ci.yml)

A small HTTP/1.1 static file server written in C++17 on raw POSIX sockets, with
no web framework or third-party HTTP library. It started as a blocking,
thread-per-request server and was rewritten around a multi-threaded event loop
built on `epoll` (Linux) and `kqueue` (macOS).

The point of the project was to understand how a server actually scales
connections at the syscall level, so the interesting part is the progression
of the concurrency model and the benchmarks that drove each change.

## Build

Requires CMake 3.10+ and a C++17 compiler.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run it, optionally overriding the defaults (port, worker count, document root):

```bash
./build/Atlas              # :8080, 10 workers, ./public
./build/Atlas 9090 4 ./www
```

### Docker

```bash
docker build -t atlas .
docker run --rm -p 8080:8080 atlas
```

Multi-stage build on `debian:bookworm-slim`, runs as a non-root user, final
image ~136MB.

## Design

Each worker is an independent reactor: its own listening socket (sharing the
port via `SO_REUSEPORT`), its own `epoll`/`kqueue` instance, and its own set of
connections. The kernel load-balances incoming connections across the workers,
so there is no shared connection state and no locking on the request path. The
only shared resource is the logger.

```
                    SO_REUSEPORT (same port, N sockets)
                 ┌──────────┬──────────┬──────────┐
   clients  ───▶ │ worker 0 │ worker 1 │  ...     │
                 │  poller  │  poller  │          │
                 │  conns   │  conns   │          │
                 └──────────┴──────────┴──────────┘
```

Within a worker, each connection is a small state machine (reading the request,
then writing the response). Sockets are non-blocking, so a slow client never
stalls the others: reads drain until `EAGAIN`, and a partially written response
is resumed on the next writable event.

The `epoll` and `kqueue` differences are hidden behind a `Poller` interface
(`add` / `modify` / `remove` / `wait`), selected at compile time. Adding a
backend means implementing four methods.

Path traversal is handled in two layers: requests containing `..` are rejected
outright, and every resolved file path is checked with `realpath` to confirm it
stays inside the document root (the containment check also rejects sibling
directories that share a name prefix). That logic is unit tested.

## Benchmarks

`wrk`, 4 threads, 100 connections, 30s, serving `index.html` over loopback.
Numbers are from a dev machine and are meant for relative comparison between
the three concurrency models. Reproduce with `./bench/run.sh`.

| Model                     | Req/sec | p50    | p99     | Dropped reads |
|---------------------------|---------|--------|---------|---------------|
| Thread pool (10 threads)  | 8,113   | 184µs  | 348µs   | 32            |
| Single event loop         | 3,668   | 2.08ms | 2.49ms  | 0             |
| Multi-reactor (10 workers)| 8,230   | 2.03ms | 44.6ms  | 0             |

What the progression showed:

- The thread pool was fast but dropped established connections once concurrency
  passed the worker count, since every worker blocks for the duration of a
  request.
- A single event loop fixed the dropped connections but lost throughput: one
  thread cannot use multiple cores, so it became the bottleneck.
- Running one event loop per core recovered the throughput while keeping zero
  dropped connections. The higher p99 is tail latency from running more reactor
  threads than cores on the test machine. Matching worker count to core count
  flattens it.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

Unit tests cover the path-containment check (including traversal and
sibling-prefix cases) and content-type resolution. CI builds on Ubuntu, runs
the unit tests, and runs an HTTP smoke test on every push.

## Limitations

- GET only. The request line is parsed but headers and bodies are ignored.
- `Connection: close` on every response, so no keep-alive.
- Static files only, read fully into memory per request (no streaming or
  caching).
