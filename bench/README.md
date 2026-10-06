# Benchmarks

Load tests using [`wrk`](https://github.com/wg/wrk) (`brew install wrk`).

```bash
./bench/run.sh [name]
```

Builds an `-O2` binary, runs `wrk` against `/`, and writes output to
`bench/results/<name>.txt`.

Defaults: 4 wrk threads, 100 connections, 30s, 10 server workers.

Numbers are loopback on a dev machine — useful for relative comparison
(e.g. thread pool vs. event loop), not as production figures.
