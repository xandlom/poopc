# Performance Optimizer Observation Platform

Stop flushing your performance down the drain.

`poopc` is a C23 port of [Andrew Kelley's `poop`](https://github.com/andrewrk/poop),
built with the [`mate.h`](https://github.com/TomasBorquez/mate.h) build system.
Output is intended to be byte-for-byte compatible with upstream. Unlike upstream,
if hardware performance counters are unavailable (hardened kernel, containers,
`perf_event_paranoid >= 3`) it degrades to reporting wall time and peak RSS
instead of aborting.

## Overview

This command line tool uses Linux's `perf_event_open` functionality to compare the performance of multiple commands
with a colorful terminal user interface.

![image](https://github.com/andrewrk/poop/assets/106511/6fc9d22b-f95b-46ce-8dc5-d5cecc77c226)

## Usage

```
Usage: poopc [options] <command1> ... <commandN>

Compares the performance of the provided commands.

Options:
 -d, --duration <ms>    (default: 5000) how long to repeatedly sample each command
 --color <when>         (default: auto) color output mode
                            available options: 'auto', 'never', 'ansi'
 -f, --allow-failures   (default: false) compare performance if a non-zero exit code is returned

```

## Building from Source

Requires a C compiler with C23 support (GCC 11+ or Clang; built with `-std=c2x`)
and a Linux target. The `mate.h` build system is vendored in this repo.

```
cc mate.c -o mate && ./mate
```

The first run compiles the bundled Samurai and builds `mate` itself; afterwards
`./mate` rebuilds only what changed. The `poopc` binary is written to
`build/bin/<arch>-<os>-<compiler>/poopc`. `./mate` also builds and runs
`render_test`, a golden-output regression check for the table renderer.

## Comparison with Hyperfine

Poop (so far) is brand new, whereas
[Hyperfine](https://github.com/sharkdp/hyperfine) is a mature project with more
configuration options and generally more polish.

However, poop does report peak memory usage as well as 5 other hardware
counters, which I personally find useful when doing performance testing. Hey,
maybe it will inspire the Hyperfine maintainers to add the extra data points!

Poop does not support running the commands in a shell. This has the upside of
not including shell spawning noise in the data points collected, and the
downside of not supporting strings inside the commands. Hyperfine by default
runs the commands in a shell, with command line options to disable this.

Poop treats the first command as a reference and the subsequent ones relative
to it, giving the user the choice of the meaning of the coloring of the deltas.
Hyperfine by default prints the wall-clock-fastest command first, with a command
line option to select a different reference command explicitly.

While Hyperfine is cross-platform, Poop is Linux-only.
