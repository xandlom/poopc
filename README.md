# Performance Optimizer Observation Platform

Stop flushing your performance down the drain.

`poopc` is a C23 port of [Andrew Kelley's `poop`](https://github.com/andrewrk/poop),
built with the [`mate.h`](https://github.com/TomasBorquez/mate.h) build system.
Output is intended to be byte-for-byte compatible with upstream. Unlike upstream,
if hardware performance counters are unavailable (hardened kernel, containers,
`perf_event_paranoid >= 3`, no `hwpmc(4)`) it degrades to reporting wall time and
peak RSS instead of aborting.

## Overview

This command line tool compares the performance of multiple commands with a colorful terminal user
interface, reading hardware performance counters directly from the OS: `perf_event_open` on Linux,
`proc_pid_rusage` on macOS and `hwpmc(4)` on FreeBSD.

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

## Platform Support

Counters come from a per-OS backend in `src/counters/`, picked by the build script; see
`src/counters.h` for the interface a new backend has to implement.

| measurement      | Linux | macOS | FreeBSD |
| ---------------- | :---: | :---: | :-----: |
| wall_time        |   ✅   |   ✅   |    ✅    |
| peak_rss         |   ✅   |   ✅   |    ✅    |
| cpu_cycles       |   ✅   |   ✅   |    ☑️    |
| instructions     |   ✅   |   ✅   |    ☑️    |
| cache_references |   ✅   |   —   |    ☑️    |
| cache_misses     |   ✅   |   —   |    ☑️    |
| branch_misses    |   ✅   |   —   |    ☑️    |

Measurements the host cannot provide are omitted from the report rather than shown as zeros.

On **macOS** the counters come from `RUSAGE_INFO_V4`, whose `ri_cycles` and `ri_instructions` are
PMU-backed and readable for any process you own — no root, no entitlement, no SIP changes. The three
cache and branch counters are only reachable through the private kperf framework, which needs root
and counts per-core rather than per-process, so they are left out. Note also that these totals
include kernel time spent on the process's behalf, whereas the Linux backend sets `exclude_kernel`;
numbers are comparable between commands measured on one machine, but not across platforms.

On **FreeBSD** all five are ☑️ rather than ✅ because which ones you get depends on the CPU: libpmc's
portable event aliases cover only some parts, event names differ between the x86 and ARMv8 PMU
classes, and a CPU may have fewer programmable slots than the five counters poopc wants. The backend
probes a list of candidate event names per counter at startup and keeps whichever the hardware
accepts, so a counter is either right or absent, never wrong. All five have been verified on a
Cortex-A72 (Raspberry Pi 4) under FreeBSD 15.0-RELEASE. Requires the `hwpmc(4)` module
(`kldload hwpmc`, or `hwpmc_load="YES"` in `/boot/loader.conf`); without it, poopc falls back to wall
time and peak RSS.

Two caveats, neither of them poopc's doing. FreeBSD's `ru_maxrss` is unreliable for very short-lived
processes, so `peak_rss` for a command like `true` swings between 0 and its real value; base's own
`/usr/bin/time -l` shows the same, so poopc reports the kernel's number as-is rather than papering
over it.

The second is a race in how hwpmc hands a process-scope counter back once its target exits, which is
not ordered against the parent waking up. When poopc loses it, `pmc_read` returns success with a
count of zero — and a lost count cannot be recovered afterwards. So poopc discards those runs instead
of recording them: a zero cycle or instruction count is impossible for a command that actually ran,
which makes it a reliable signal. Roughly one run in eight hundred is dropped this way, which only
means a slightly lower run count for the same wall time. If it happens constantly, poopc says so and
finishes on wall time and peak RSS.

Other platforms build and run, reporting wall time and peak RSS only.

## Building from Source

Requires a C compiler with C23 support (GCC 11+ or Clang; built with `-std=c2x`)
and a Linux, macOS or FreeBSD target. The `mate.h` build system is vendored in
this repo.

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

Hyperfine is cross-platform; poopc runs on Linux, macOS and FreeBSD, with the full set of hardware
counters only on Linux.
