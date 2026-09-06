#define MATE_IMPLEMENTATION
#include "mate.h"

// Build script for poopc. Bootstrap with:  cc mate.c -o mate && ./mate
// After the first run, `./mate` rebuilds itself as needed via mate-cache.ini.
int main(void) {
  Target t = HostTarget();

  StartBuild();
  {
    Executable exe = CreateExecutable((ExecutableOptions){
        .output = "poopc",
        .warnings = FLAG_WARNINGS,         // -Wall -Wextra
        .optimization = FLAG_OPTIMIZATION, // -O2
        .std = FLAG_STD_C2X,               // portable C23 subset: gcc 11+ and clang
        .flags = "-D_GNU_SOURCE",
    });

    AddFile(exe, "./src/poop.c");
    AddFile(exe, "./src/perf.c");
    AddFile(exe, "./src/stats.c");
    AddFile(exe, "./src/term.c");
    AddFile(exe, "./src/progress.c");
    AddFile(exe, "./src/report.c");

    if (isLinux(t)) {
      LinkSystemLibraries(exe, "m"); // sqrt() from <math.h>
    }

    InstallExecutable(exe);

    errno_t cc_err = CreateCompileCommands(exe); // compile_commands.json for clangd
    (void)cc_err;

    // Golden-output regression test for the rendering code.
    Executable render_test = CreateExecutable((ExecutableOptions){
        .output = "render_test",
        .warnings = FLAG_WARNINGS,
        .std = FLAG_STD_C2X,
        .flags = "-D_GNU_SOURCE",
        .includes = "-I./src",
    });
    AddFile(render_test, "./tests/render_test.c");
    AddFile(render_test, "./src/report.c");
    AddFile(render_test, "./src/term.c");
    AddFile(render_test, "./src/stats.c");
    if (isLinux(t)) {
      LinkSystemLibraries(render_test, "m");
    }
    InstallExecutable(render_test);

    errno_t test_err = RunCommandF("%s check tests/render_golden.txt",
                                   render_test.outputPath.data);
    Assert(test_err == SUCCESS, "render_test: golden-output mismatch");
  }
  EndBuild();
}
