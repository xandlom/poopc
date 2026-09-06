#define MATE_IMPLEMENTATION
#include "mate.h"

// Build script for poopc. Bootstrap with:  cc mate.c -o mate && ./mate
// After the first run, `./mate` rebuilds itself as needed via mate-cache.ini.
int main(void) {
  Target t = HostTarget();

  // Hardware counters come from a per-OS backend implementing src/counters.h.
  // Anything without one still builds, reporting wall time and peak RSS only.
  char *counters_backend = "./src/counters/unsupported.c";
  if (isLinux(t))        counters_backend = "./src/counters/linux.c";
  else if (isMacOS(t))   counters_backend = "./src/counters/darwin.c";
  else if (isFreeBSD(t)) counters_backend = "./src/counters/freebsd.c";

  // _GNU_SOURCE is what Linux needs for perf_event_open's syscall plumbing;
  // Darwin and FreeBSD expose everything used here by default.
  char *flags = isLinux(t) ? "-D_GNU_SOURCE" : "";

  StartBuild();
  {
    Executable exe = CreateExecutable((ExecutableOptions){
        .output = "poopc",
        .warnings = FLAG_WARNINGS,         // -Wall -Wextra
        .optimization = FLAG_OPTIMIZATION, // -O2
        .std = FLAG_STD_C2X,               // portable C23 subset: gcc 11+ and clang
        .flags = flags,
    });

    AddFile(exe, "./src/poop.c");
    AddFile(exe, "./src/child.c");
    AddFile(exe, counters_backend);
    AddFile(exe, "./src/stats.c");
    AddFile(exe, "./src/term.c");
    AddFile(exe, "./src/progress.c");
    AddFile(exe, "./src/report.c");

    // Darwin folds libm into libSystem; the others need it for sqrt().
    if (!isMacOS(t)) {
      LinkSystemLibraries(exe, "m");
    }
    if (isFreeBSD(t)) {
      LinkSystemLibraries(exe, "pmc"); // hwpmc(4) counters
    }

    InstallExecutable(exe);

    errno_t cc_err = CreateCompileCommands(exe); // compile_commands.json for clangd
    (void)cc_err;

    // Golden-output regression test for the rendering code.
    Executable render_test = CreateExecutable((ExecutableOptions){
        .output = "render_test",
        .warnings = FLAG_WARNINGS,
        .std = FLAG_STD_C2X,
        .flags = flags,
        .includes = "-I./src",
    });
    AddFile(render_test, "./tests/render_test.c");
    AddFile(render_test, "./src/report.c");
    AddFile(render_test, "./src/term.c");
    AddFile(render_test, "./src/stats.c");
    if (!isMacOS(t)) {
      LinkSystemLibraries(render_test, "m");
    }
    InstallExecutable(render_test);

    errno_t test_err = RunCommandF("%s check tests/render_golden.txt",
                                   render_test.outputPath.data);
    Assert(test_err == SUCCESS, "render_test: golden-output mismatch");
  }
  EndBuild();
}
