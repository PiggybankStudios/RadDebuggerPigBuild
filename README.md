# PigBuild Script for RAD Debugger

This repository contains an experimental way to compile [RAD Debugger](https://github.com/EpicGamesExt/raddebugger) that uses [PigBuild](https://github.com/PiggybankStudios/PigBuild). To use it clone or download RAD Debugger source code and copy all files in this repository into the root directory (replace `build.bat`/`build.sh`, don't copy this `README.md`). Then simply run `build.bat` or `build.sh`, you can pass arguments like you would for the original build scripts, like `build.bat raddbg clang release`. Or you can change `DEFAULT_CMD_LINE_ARGS` in `build_script.c` and run `build.bat`/`build.sh` with no arguments.

## Build Options

* `debug` / `release` => Compile the target(s) in debug or release mode. This changes various compiler options related to optimization and debug symbol generation and also changes the value of the `#define BUILD_DEBUG=1/0`
* `msvc` / `clang` => Use MSVC or Clang compiler. Default is MSVC on Windows, and Clang on other platforms
* `target` => Add one or more targets that you want to be built. See `build_targets.c` for a list of target names that can be specified. If no target is given, then `raddbg` will be automatically added as a default target
* `telemetry` / `spall` => Enable Telemetry or Spall profiling features (must have appropriate headers place in `telemetry/` or `spall/`)
* `asan` / `ubsan` => Enable Address Sanatizer or Undefined Behavior Sanatizer (only supported on Clang currently)
* `opengl` => Use OpenGL rendering backend
* `dwarf` => Use DWARF debug symbol format instead of CodeView (only affects Clang)

## How Does it Work?

PigBuild is small framework for writing build scripts in C with minimal dependence on shell. It compiles `build_script.c` into a `builder.exe` first and then runs that. The builder has all the "real" build logic that parses command-line options and decides what compiler options to pass and what source files to target. The `builder.exe` only needs to be built whenever the `build_script.c` or it's dependent files are changed.

The build script uses `RecompileIfNeeded(paths)` at the top of the main entry point to check if any of it's source files have changed since the last time it was built and exits with a special exit code that tells the `build.bat`/`build.sh` it needs a recompile. This saves a little time on every compilation by not needing to run the compiler/linker to get a new `builder.exe` if it already exists. That time adds up for every compile so the added complexity is worth it. We also may need to run `VsDevCmd.bat` if compiling on Windows with MSVC compiler and that takes 2-5 seconds which is a LOT of time to save so this gated recompilation is quite important.

The first time you run the `build.bat`/`build.sh` it will check to see if the `pig_build` folder is present. If not it will attempt to `git clone https://github.com/PiggybankStudios/PigBuild pig_build`. You can download PigBuild manually if you would prefer to get it a copy of it in some other way. Eventually if PigBuild was integrated into the project the source code from PigBuild could be directly added to the RAD Debugger repository.

One benefit of PigBuild over the old approach is that the majority of the logic for all platforms is in a single file (build_script.c) rather than split between two shell scripts. Although we still have a `build.bat` and `build.sh`, almost none of the RAD Debugger build logic lives in those files. The shell scripts are usually entirely the same for all PigBuild projects since they only need to know how to compile a `builder.exe` on the main desktop platforms. The only differences are how we find PigBuild's installation location and extra compiler flags that may be needed to get the `build_script.c` to compile (right now we add an include directory for the `src/` folder because `metagen_main.c` pulls in `src/base/` and `src/mdesk` files with `#include` paths relative to that folder).

## Metagen in Builder

RAD Debugger already has a C script that needs to get built and run as part of the build process. This `metagen.exe` can get folded directly into the `builder.exe`. Doing code generation and meta-programming in general is one of the best use cases for PigBuild since you are writing your build logic in a full programming language instead of whatever DSL you would write for other build systems. The logic for complex programs like this is also easy to read since it's in the same language as the main project. You don't have to learn the nuances of bash or batch in order to understand how the project gets built or how code-generation works.

You can also debug your build logic with a regular debugger. The `build_script.c` gets compiled into a executable with debug symbols and you can run that executable directly, once it's built by the shell script. All command-line options are passed through the `build.bat` to the `builder.exe`, so the runtime behavior should be identical. The only caveat is that you will need to run the `build.bat` if the builder exits with the recompile request exit code.

Currently the way we include `metagen_main.c` in `build_script.c` is somewhat ugly. There are some naming conflicts between code in PigBuild and `src/base` and the main entrypoints need to be renamed so that we have a "real" entrypoint in the `build_script.c` and we can call the renamed entrypoint when we are ready to do metagen. This all could be easily fixed if a few changes were made to the RAD Debugger codebase but I've opted to get this working with a clone of the codebase as it currently is to demonstrate that it can work.

## Work in Progress

PigBuild is a new repository that is being actively developed (started in March 2026) and still has a lot of bugs and imperfections. RAD Debugger is serving the role of a "guinea pig", allowing me to see what a build script might look like for a production codebase. PigBuild (and [PigCore](https://github.com/PiggybankStudios/PigCore)) are inspired by the RAD Debugger codebase and the [Digital Grove](https://www.dgtlgrove.com/) repositories so it's a rather ideal scenario. I will be updating this `build_script.c` as I make changes to PigBuild and hopefully things will become more and more readable and simple over time.

The `build_script.c` is currently longer than the two shell scripts that were used to build the project before. I would argue that the C code is already more readable and simple feeling than those shell scripts but you could argue that some aspects are a little less clear than the old approach.

The C code in PigBuild has some stylistic differences to the code in `src/`. These differences could be easily changed to try and make the `build_script.c` read very similar to the main source code. Also, similar to how `metagen.exe` works, anything from `src/` that you want to use in the `build_script.c` and doesn't need metaprogramming or special compiler flags could be used instead of similar parts of PigBuild.

### TODO List

- [ ] Add support for compiling on Linux
- [ ] Add support for GCC compiler
- [ ] Finish support for finding `VsDevCmd.bat` or `vcvarsall.bat` and running it or reporting that we couldn't find it to the user
- [ ] Can we do asan and ubsan with MSVC compiler?
- [ ] Get all targets working
	- [x] raddbg
	- [x] radlink
	- [x] radbin
	- [x] debugstringperf
	- [x] torture
	- [ ] raddump
	- [ ] ryan_scratch
	- [ ] textperf
	- [ ] convertperf
	- [ ] parse_inline_sites
	- [ ] strip_lib_debug
	- [ ] mule_main
	- [ ] mule_module
	- [ ] mule_hotload
	- [ ] dwarf_expr_test
	- [ ] mule_peb_trample
- [ ] Add support for all command-line options
	- [x] msvc
	- [x] clang
	- [ ] gcc
	- [x] release
	- [x] debug
	- [x] telemetry (check)
	- [x] spall
	- [x] asan
	- [x] ubsan
	- [x] opengl
	- [x] dwarf (check)
	- [ ] pgo
- [ ] Add new-to-me compiler options to `pig_build_cli_flags.h` to give them readable names and descriptive comments
- [x] Add an "all" command-line option that enables all targets
- [ ] Add a "clean" option to remove old build artifacts and force regeneration of things like logo.res
- [ ] Convert naming conventions and other stylistic choices to match RAD Debugger source code (fork of PigBuild?)
- [ ] Add a way to build all permutations in a single go. (debug/relase, msvc/clang/gcc, asan, ubsan, opengl, etc.)
- [ ] Simplify command-line argument handling with better string splitting and StrArray management
- [ ] Make sure everything works with weird file paths (spaces in path to raddebugger folder, non-ASCII characters, etc.)
- [ ] Make sure this all works as part of CI/CD systems like GitHub Actions
- [ ] Make PrintLine/PrintLine_E outputs route to OutputDebugString on Windows so they show up in debuggers
- [ ] Don't ask for recompilation if running builder.exe inside a debugger
