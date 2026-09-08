# Windows port: current status

**Confirmed working on a real Windows machine, MSYS2/MinGW toolchain, via
GitHub Actions CI: configure, full build (core + CLI + GUI + tests, ctest
all green), `windeployqt` + the runtime DLL bundling below, and
`EpochFrom-gui.exe` actually launching and running.** This document tracks
what got it there, what a Windows build needs from you, and what's still
genuinely unverified (MSVC/vcpkg hasn't been attempted at all -- only
MSYS2/MinGW has been confirmed end to end).

Getting here took several real rounds of CI failures and fixes -- wcslib's
own dis.h-documented SIP requirements, MinGW's runtime DLLs not being
something windeployqt knows to bundle, cfitsio's own libcurl dependency,
and more -- each one fixed and reverified against this repo's own Linux
build before being sent back. The sections below are what's left standing
after that process, not a first-draft guess.

## What already works without changes

Qt6 and Eigen3 both have first-class Windows support (the official Qt
installer, or vcpkg) and needed nothing new. The C++ codebase itself was
already portable before this pass: no `fork()`/`system()`/POSIX-only file
APIs, every path goes through `QDir`/`QFileInfo` rather than a hardcoded
`/`, and both external processes EpochFrom shells out to (`solve-field`,
`scripts/gaia_field_query.py`) are invoked via `QProcess` argument lists,
not a shell string -- no Windows shell-quoting surprises there.

## What this pass changed

- **wcslib/cfitsio dependency resolution** (top-level `CMakeLists.txt`,
  new `cmake/FindWCSLIB.cmake`): previously a hard `pkg_check_modules(...
  REQUIRED)`, which only works where pkg-config and both libraries' `.pc`
  files exist. Now tries pkg-config first (Linux, and Windows via
  MSYS2/MinGW, which packages both as regular pacman packages), and falls
  back to plain `find_package()` for an MSVC/vcpkg build -- see
  [Toolchain options](#toolchain-options) below for what that fallback
  actually requires from you today. Both paths converge on two
  OS-agnostic targets, `EpochFrom::WCSLIB` and `EpochFrom::CFITSIO`, that
  the rest of the build links against unconditionally.
- **`packaging/windows/epochfrom.ico`, `epochfrom-gui.rc`,
  `epochfrom-starfield.rc`**: a compiled-in Windows icon resource for
  each `.exe`, wired into `src/gui/CMakeLists.txt` under `if(WIN32)`.
  This is a different mechanism from `AppIcon.h`'s
  `QApplication::setWindowIcon()` call, which already works cross-platform
  with no install step (see that file's own comment) -- what only a `.rc`
  resource gets you is an icon on the `.exe` *file itself*: File Explorer,
  a taskbar pin made before the app has ever been run, a desktop shortcut.
  Not yet verified to actually render correctly once compiled in.
- **`GaiaTab.cpp`'s default Python interpreter path**: was hardcoded to
  `"python3"`, which isn't on `PATH` by default on a stock Windows Python
  install (`python`/the `py` launcher are the norm there). Now
  `Q_OS_WIN`-conditional. It's only the field's starting value either
  way -- always editable.

## Toolchain options

Both were requested; neither has been build-tested yet.

### MSYS2 + MinGW

Closer to the Linux toolchain already in use -- pkg-config works the same
way, and MSYS2's package repo already has most of what's needed:

```
pacman -S mingw-w64-x86_64-qt6-base mingw-w64-x86_64-eigen3 \
          mingw-w64-x86_64-cfitsio mingw-w64-x86_64-wcslib \
          mingw-w64-x86_64-cmake mingw-w64-x86_64-pkgconf \
          mingw-w64-x86_64-toolchain
```

**Update:** wcslib *is* packaged in MSYS2 (confirmed against a real CI run:
`mingw-w64-x86_64-wcslib` resolves via pkg-config, version 8.9) -- the
original note here saying otherwise was wrong; no from-source build needed
after all. What that same CI run did surface: MSYS2's `cfitsio.pc` embeds a
literal `prefix=/mingw64` (a path only meaningful inside the MSYS2
runtime's own translation layer), which a native `cmake.exe` takes
literally and fails to resolve -- `CMakeLists.txt` now rewrites that to the
real MinGW root itself (derived from the active compiler's location) right
after pkg-config hands the target back, so this shouldn't need any special
handling from you.

### MSVC + vcpkg

The repo ships a `vcpkg.json` manifest (qtbase, eigen3, cfitsio) at its
root, so there's no separate `vcpkg install` step -- passing the toolchain
file is enough, and vcpkg installs the manifest's dependencies
automatically as part of configure:

```
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg root>/scripts/buildsystems/vcpkg.cmake
```

**Don't run `vcpkg install qtbase eigen3 cfitsio` by hand** -- that's
*classic mode*, and the vcpkg that ships with recent Visual Studio (the
"vcpkg package manager" installer component) has classic mode disabled
entirely, failing with `This vcpkg distribution does not have a classic
mode instance.` Manifest mode -- the `cmake -B ...` command above, run
from the repo root so the toolchain file finds `vcpkg.json` next to it --
is what that distribution actually expects, and is also just the
currently-recommended way to use vcpkg regardless of which distribution
you have.

cfitsio's vcpkg port existing is good news; whether it exports a CMake
config your `find_package(CFITSIO CONFIG QUIET)` actually picks up (this
pass's CMake code guesses at both a plain `cfitsio` target and a
`CFITSIO::CFITSIO` one, since vcpkg ports have used both conventions
historically) needs confirming against whatever the current vcpkg
registry actually ships.

**wcslib has no vcpkg port.** It needs to be built manually (it's a
plain autotools project; a from-scratch Windows build of it is its own
piece of work this pass hasn't attempted) and then pointed at via:

```
cmake -B build -S . -DCMAKE_PREFIX_PATH=<wcslib install prefix> ...
```

`cmake/FindWCSLIB.cmake` picks it up from there -- see that file for the
exact search hints (`WCSLIB_ROOT` also works, as an alternative to folding
it into `CMAKE_PREFIX_PATH`).

## Plate-solving: ANSVR

`solve-field` (astrometry.net) has no native Windows build. EpochFrom
doesn't attempt to work around this -- `PlateSolver`'s `solveFieldPath`
option (the Solve tab's "solve-field path" field, or `--solve-field-path`
on the CLI) already just takes whatever's on `PATH` or an explicit path,
so the intended route is [ansvr](https://adgsoftware.com/ansvr/) (or a
current equivalent), the local astrometry.net solver already used by a
lot of the PixInsight-on-Windows community, pointed at from that setting.

**Update, from real use:** pointing `solveFieldPath` directly at
`solve-field` inside an ansvr install (typically
`%LOCALAPPDATA%\cygwin_ansvr\...`) fails with EpochFrom's own "failed to
start ... -- is astrometry.net installed and on PATH?" message, no matter
the exact path or whether `.exe` is appended. Cause: ansvr's `solve-field`
is astrometry.net's own upstream Perl script, not a compiled `.exe` -- it
only runs inside the Cygwin environment ansvr bundles, and Windows'
`CreateProcess` (what Qt's `QProcess` uses under the hood) has no concept
of a Perl shebang line, so it can't launch the script directly at all.
The documented, working pattern (also how other Windows astronomy
software calls into ansvr) is to invoke it through Cygwin's own shell:
`bash.exe --login -c "solve-field ..."`.
[`packaging/windows/ansvr-solve-field.bat`](../packaging/windows/ansvr-solve-field.bat)
wraps exactly that -- point EpochFrom's "solve-field path" setting at
that `.bat` file (Qt's `QProcess` launches `.bat`/`.cmd` files on Windows
fine) instead of at `solve-field` itself. **Confirmed on a real ansvr
install:** this gets EpochFrom past "failed to start" and solve-field's
own output starts coming through.

**One caveat this hasn't been exercised against yet:** the wrapper
forwards arguments through cmd.exe's `%*` into a single `bash -c` string,
and an argument containing spaces (a Windows image path with spaces in
it, for instance) could end up mis-quoted once it's re-embedded that way
-- this hasn't come up in testing so far, but if a solve fails specifically
on a path with spaces where a similar path without spaces works, that's
the first thing to suspect.

The next issue that surfaced once solve-field could actually start:
ansvr bundles an older astrometry.net build that doesn't recognize the
`--temp-axy` flag (`solve-field: unknown option -- temp-axy`) --
`PlateSolver::solve()` used to pass that to keep its intermediate `.axy`
file out of the image's directory. First fix attempt (`--axy <temp
path>`, on the assumption that flag predates `--temp-axy`) turned out
wrong too -- same ansvr build rejected that one as well ("unknown option
-- axy"), confirmed via real use. Rather than keep guessing at this
particular build's exact flag vocabulary blind, `PlateSolver::solve()`
now passes **no** `.axy`-related flag at all: every astrometry.net build,
however old, writes its intermediate axy file to a plain, flag-free
default location (`<base>.axy`, right beside the image) -- EpochFrom just
deletes that file itself once solve-field exits, on every code path,
instead of asking solve-field to redirect it anywhere. Verified against
this repo's own Linux build (9/9 tests still pass) -- **not yet confirmed
against a real ansvr solve**, since that requires an actual solve to run
to completion, which is the next thing to try.

Beyond that, whether ansvr's `solve-field` accepts the rest of the flags
`PlateSolver::solve()` passes (`--ra`/`--dec`, `--scale-low`/
`--scale-high`, `--downsample`, `--cpulimit`, the other
byproduct-suppression flags) is still unverified -- each one so far has
surfaced as its own distinct error once the previous one was fixed, so
more of the same is possible. If another "unknown option" turns up,
pasting this build's own `solve-field --help` output (via the wrapper --
`ansvr-solve-field.bat --help`) would settle its exact supported flag set
in one step rather than continuing to find out by trial and error.

**Getting `ansvr-solve-field.bat` into a built package:** it lives in the
repo at `packaging/windows/`, not in `dist/bin` -- nothing in the CMake
build or a from-scratch CI workflow copies it there automatically yet
(unlike the `.rc`/`.ico` resources, which *are* wired into the build).
Until that's added to the build, add one line copying it alongside the
`.exe`s in whatever CI step already copies the MinGW/cfitsio DLLs (see
the next section below), e.g.:

```bash
cp "$GITHUB_WORKSPACE/packaging/windows/ansvr-solve-field.bat" .
```

or, building locally, just copy it into the same folder as
`EpochFrom-gui.exe` by hand.

## Deploying a MinGW build: DLLs windeployqt doesn't know about

`windeployqt.exe EpochFrom-gui.exe` only copies Qt's own dependencies; it
has no concept of the MinGW toolchain's own runtime, or of wcslib/cfitsio.
Confirmed on a real Windows run: the `.exe` fails to start with a missing-
DLL dialog for each of these until they're dealt with. `-static-libgcc
-static-libstdc++` (in the top-level `CMakeLists.txt`, under `if(MINGW)`)
reliably eliminates the need for `libgcc_s_seh-1.dll` and `libstdc++-6.dll`
-- confirmed fixed on a real run. `libwinpthread-1.dll` resisted the same
treatment (neither a plain `-static -lwinpthread` nor an explicit
`-Wl,-Bstatic,--whole-archive`/`-Bdynamic` wrapper kept it out of the
`.exe`'s import table on a real run, most likely because Qt's own
transitive link interface pulls it back in dynamically later in the link
line) -- rather than keep guessing at MinGW/CMake link-line ordering with
no MinGW toolchain available to test against, the practical fix is to just
copy it, the same way the deploy step already has to for cfitsio (wcslib
itself turned out to be static-only on MSYS2, per real CI output, so
nothing to copy there).

**Copying DLLs by hand one missing-DLL-dialog at a time doesn't scale --
confirmed on a real run: after libwinpthread and cfitsio, the next missing
one was libcurl-4.dll**, because MSYS2's cfitsio is built with libcurl
support (for fetching remote FITS URLs -- unused by EpochFrom, but linked
in regardless), and libcurl itself commonly pulls in its own chain of
dependencies (TLS, compression, IDN, and so on -- however many of those
MSYS2's libcurl build actually needs, which isn't worth enumerating by
hand). The reliable fix is to resolve the whole dependency tree at once
with `ldd` (available in the MSYS2 shell) instead of adding one `cp` line
per CI failure:

```bash
# Run from the directory windeployqt already populated, after building
# both executables. Copies every MinGW-provided DLL either one actually
# needs, transitively -- not just Qt's own, which windeployqt already
# handled.
for exe in EpochFrom-gui.exe EpochFrom-starfield.exe; do
    ldd "$exe" | grep -i '/mingw64/bin/' | awk '{print $3}'
done | sort -u | xargs -I{} cp -n {} .
```

`ldd` walks the *actual* runtime dependency graph the way the Windows
loader will, so this covers whatever libcurl (or anything else) needs
without guessing -- and stays correct if that dependency set changes on a
future MSYS2 update. `-n` (no-clobber) skips files already copied (Qt's
own DLLs, already placed by `windeployqt`).

## Known limitations even once it builds

- **Solve cancellation doesn't kill `solve-field`'s full process tree on
  Windows.** On Unix, a timeout sends `SIGKILL` to the whole process
  group (see the comment in `PlateSolver::solve()`); Windows falls back to
  killing only the direct child, which can leave a grandchild process
  (e.g. an augment-xylist stage) still running. A real fix exists -- a Job
  Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, assigned via
  `QProcess::setCreateProcessArgumentsModifier` -- just not implemented
  yet.
- **No macOS story.** `if(UNIX AND NOT APPLE)` already keeps the Linux
  `.desktop`/hicolor-icon-theme install from running on macOS, but nothing
  Windows- or macOS-specific has been added for that platform either.
- **No installer.** `cmake --install` places binaries + (on Linux) desktop
  integration; there's no Windows installer (MSIX, Inno Setup, etc.) or
  self-contained `windeployqt`-based redistribution step yet.

## What to actually do first

**MSYS2/MinGW is done and confirmed working end to end** -- configure,
build, tests, deploy, and a real launch of `EpochFrom-gui.exe`. If you're
setting up a Windows build, use that toolchain; it's the one this page's
guidance has actually been exercised against. MSVC/vcpkg remains
completely unattempted -- wcslib's lack of a vcpkg port is still the
biggest open unknown there, and nothing in this document's MSVC section
has been verified against a real MSVC build the way the MinGW path now
has.

The one piece of actual EpochFrom functionality still unverified on
Windows is plate-solving itself -- see [Plate-solving:
ANSVR](#plate-solving-ansvr) above. `EpochFrom-gui.exe` correctly reports
`failed to start 'solve-field' -- is astrometry.net installed and on
PATH?` when no solver is configured; that's expected, not a bug, until
ANSVR (or an equivalent) is installed and pointed at from the Solve tab's
"solve-field path" setting.
