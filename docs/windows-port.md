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
[`scripts/ansvr-solve-field.bat`](../scripts/ansvr-solve-field.bat)
wraps exactly that -- point EpochFrom's "solve-field path" setting at
that `.bat` file (Qt's `QProcess` launches `.bat`/`.cmd` files on Windows
fine) instead of at `solve-field` itself. **Confirmed on a real ansvr
install:** this gets EpochFrom past "failed to start" and solve-field's
own output starts coming through. **Update:** the Solve tab now
auto-detects it there and pre-fills "solve-field path" with it directly
(`SolveTab::findAnsvrWrapper()`, same mechanism as the Gaia tab's script
auto-detection) -- on a normal Windows build, there's nothing to browse
to or type in for this at all anymore.

**Update, from real use: that caveat was real.** An image path containing
a space (ordinary for astrophotography capture software's own
auto-generated filenames, e.g. `2017-04-29 21-15-33 M31.fits`) got
silently truncated at the first space -- solve-field only ever saw
`2017-04-29`, confirmed via a real solve failure quoting exactly that.
Cause: the original wrapper spliced `%*` directly into a single, already
double-quoted `bash -c "solve-field %*"` string, which then gets parsed
*twice* with two different quoting rules -- once by Windows' own argv
parser building `bash.exe`'s command line, then again by bash's own `-c`
string word-splitting -- and a space-containing argument doesn't survive
both intact. Fixed: the script now passes the real arguments as `bash
-c`'s own trailing arguments (parsed exactly once, by the same
Windows argv-quoting rules Qt's `QProcess` used to build them) and keeps
the `-c` script itself fixed and tiny -- `exec solve-field "$@"` --
forwarding those already-intact arguments through verbatim rather than
re-embedding them as text. Not yet reconfirmed against a real solve with
a spaced filename since this fix (that's the next thing to try).

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

**Getting `ansvr-solve-field.bat` into a built package: fixed, now
automatic.** It lives in the repo at `scripts/ansvr-solve-field.bat`,
right beside `gaia_field_query.py` -- it used to sit under
`packaging/windows/` instead, requiring a hand-maintained CI copy step
of its own, which went through several real rounds of trouble before it
ever actually worked (see the two sub-points below, kept for the
general lessons they carry even though this specific problem no longer
needs them). `src/gui/CMakeLists.txt` copies the whole `scripts/`
directory next to `EpochFrom-gui` on every build (`POST_BUILD`, so it's
there in the raw build tree too, not only after `cmake --install`) and
via an `install()` rule -- both files come along together, on every
platform (the `.bat` is simply inert and unused off Windows), so it's
just present in a normal build's output from now on, with no CI YAML to
keep in sync with this repo, and nothing further to do here. The Solve
tab also auto-detects it at that path and pre-fills "solve-field path"
with it directly (`SolveTab::findAnsvrWrapper()`), the same way the Gaia
tab already did for its own script.

The two lessons from getting there, still worth knowing for any
*other* file a CI step needs to copy out of the checkout:

- **`$GITHUB_WORKSPACE` needs `cygpath -u` in an MSYS2 shell step.** It's
  set by the Actions runner itself, as a plain Windows-style path with
  backslashes, and MSYS2 doesn't retroactively translate an env var it
  didn't create -- splicing it straight into a `/`-separated path mixes
  both separators in one string, which MSYS2's `cp` doesn't reliably
  resolve. `cp "$(cygpath -u "$GITHUB_WORKSPACE")/<path>" .` avoids that.
- **Watch for a `checkout@v4` step using `with: path: <something>`.** If
  a workflow checks the repo out into a named subdirectory rather than
  `$GITHUB_WORKSPACE` directly (`actions/checkout`'s `path:` input),
  every path built from `$GITHUB_WORKSPACE` needs that subdirectory
  folded in too -- `$GITHUB_WORKSPACE` alone still points at the plain
  workspace root, one level *above* where the actual checked-out source
  really lives. A build step whose own output prefix (e.g. `dist/`) is
  workspace-relative rather than checkout-relative can end up at a
  different depth than the source tree without that being obvious from
  the error alone (a bare "No such file or directory" looks identical
  either way).

## ansvr's own Cygwin environment: fork failures

Confirmed on a real solve attempt, unrelated to anything above: ansvr's
bundled Python 2 (used internally by astrometry.net's own
`image2pnm.py`, part of its image-loading pipeline) failed with

```
child_info_fork::abort: address space needed by 'cygcrypto-1.0.0.dll'
(0x1770000) is already occupied
...
OSError: [Errno 11] Resource temporarily unavailable
```

This is a long-standing, well-known Cygwin issue, nothing specific to
EpochFrom or this port: Cygwin emulates POSIX `fork()` on top of Windows
(which has no native equivalent), by requiring every DLL involved to
re-map at the exact same virtual address in the child process as it held
in the parent. When something else already occupies that address in the
child at the moment of the fork -- another DLL loaded by antivirus/EDR
software injecting into the process, a Windows update that shifted a
system DLL's base address, or just accumulated drift since ansvr's DLLs
were originally built -- the fork fails outright with exactly this
error. It's an ansvr/Cygwin-installation issue on the machine running
EpochFrom, not something a `PlateSolver.cpp` change can fix.

The standard Cygwin fix is `rebaseall`, which reassigns every DLL in a
Cygwin installation a fresh, non-conflicting base address:

1. Close every ansvr/Cygwin-using process first (including EpochFrom, if
   it has a solve in flight) -- `rebaseall` can't rewrite a DLL that's
   currently mapped into a running process.
2. From `%LOCALAPPDATA%\cygwin_ansvr\bin`, run `ash.exe` (not
   `bash.exe` -- `ash` is the minimal shell Cygwin's own tooling uses
   specifically for this, since it doesn't hold to fewer of the Cygwin
   DLLs' handles open than `bash` would).  Run it as Administrator if
   plain rebasing fails with a permissions error.
3. In that `ash` shell: `/bin/rebaseall -v`
4. Close `ash`, then try solving again.

**Confirmed on a real ansvr install** -- with one real-world wrinkle:
`rebaseall` kept refusing ("only ash or dash processes are allowed"),
including immediately after a full reboot, which ruled out an ordinary
leftover handle. Cause: ansvr registers its own background server,
`start_ansvr.bat`, to auto-launch at Windows login (ansvr can run as a
persistent local HTTP solver, not just be invoked one-shot the way
EpochFrom's wrapper uses it) -- that recreated a live Cygwin process
before `ash` was ever run, on every boot. Fix: **Task Manager -> Startup
tab**, find the `ansvr` entry, disable it. EpochFrom never needs ansvr's
background server (it always invokes `solve-field` directly through the
wrapper), so leaving that startup entry disabled permanently is fine --
once it's gone, `rebaseall` runs cleanly. If a fork failure ever comes
back after this, antivirus/EDR software injecting a DLL into every new
process is the next thing to suspect.

## ansvr / Cygwin FAQ

Everything above this point tells the story of how each of these was
found; this is the fast lookup for someone hitting one of them fresh.
All of it is specific to ansvr's particular (old) Cygwin-based build --
none of it is EpochFrom-specific beyond the fixes already shipped in this
repo.

**"failed to start '...' -- is astrometry.net installed and on PATH?"
even though ansvr is installed and the path looks right.**
`solve-field` is astrometry.net's own upstream Perl script, not a
compiled `.exe` -- Windows' `CreateProcess` (what Qt's `QProcess` uses)
can't launch a Perl-shebang script directly, no matter how correct the
path to it is. Point EpochFrom's "solve-field path" setting at
[`scripts/ansvr-solve-field.bat`](../scripts/ansvr-solve-field.bat)
instead of at `solve-field` itself -- it routes the call through
Cygwin's own `bash.exe`, which is what actually works. Confirmed on a
real ansvr install. On a normal build this field auto-detects and
pre-fills that path on its own (the Solve tab looks for it right next to
`EpochFrom-gui.exe`, in the `scripts` subfolder), so there's usually
nothing to set here manually at all.

**`solve-field: unknown option -- temp-axy` or `unknown option --
axy`.** ansvr bundles an old astrometry.net build that rejects both of
solve-field's own ways to redirect its intermediate `.axy` file.
Already fixed in this repo's `PlateSolver.cpp` (as of the commit that
removed the `--axy` flag entirely, letting solve-field use its
plain, flag-free default location and deleting that file itself
afterward) -- if you're still seeing this, you're running an `.exe`
built before that fix landed; grab a current build.

**An image path with a space in it gets silently truncated (solve-field
reports reading a nonsense partial filename).** Bug in an earlier
version of `ansvr-solve-field.bat`: splicing `%*` straight into an
already-quoted `bash -c "solve-field %*"` string gets parsed twice
(once by Windows building `bash.exe`'s command line, again by bash's own
`-c` word-splitting), and a space-containing argument doesn't survive
both passes intact. Fixed in the current version of the script (passes
arguments as `bash -c`'s own trailing arguments, forwarded verbatim via
`"$@"`, instead of splicing them into the script text) -- if you're
still hitting this, make sure you have the current `.bat`, not a copy
made before this fix.

**`ansvr-solve-field.bat` (or `scripts/gaia_field_query.py`) isn't in
your downloaded/built Windows package.** Fixed -- `src/gui/CMakeLists.txt`
now copies both next to `EpochFrom-gui` automatically, at build time and
via `install()`; see "Getting `ansvr-solve-field.bat` into a built
package" above. If you're still not finding one of them, you're most
likely running a build from before that CMake change -- get a current
build. (If you're maintaining a *custom* packaging step that copies
files out of the checkout for some other reason, the two path lessons
just above -- `cygpath -u` and the `checkout@v4` `path:` gotcha -- are
what to reach for.)

**`child_info_fork::abort: address space needed by '...dll' is already
occupied`, or a Python `OSError: [Errno 11] Resource temporarily
unavailable` from something in `astrometry/bin`.** A long-standing,
well-known Cygwin issue (fork() emulation requires a DLL to remap at the
exact same address in the child process as the parent held), not an
EpochFrom bug. Fix: run `/bin/rebaseall -v` from
`%LOCALAPPDATA%\cygwin_ansvr\bin\ash.exe` (not `bash.exe`) with every
other ansvr/Cygwin process closed first, then retry the solve. See
"ansvr's own Cygwin environment: fork failures" above for the full
walkthrough.

**`rebaseall` refuses with "only ash or dash processes are allowed
during rebasing", but nothing obviously Cygwin-related is running --
including right after a full reboot.** **Confirmed real-world cause:**
ansvr registers its own background solver server, `start_ansvr.bat`, to
auto-launch at Windows login -- that recreates a live Cygwin process
before `ash` is ever run, on every single boot, which is exactly why
rebooting alone doesn't clear it. Fix: **Task Manager -> Startup tab**,
find the `ansvr` entry, disable it (EpochFrom never needs ansvr's
background server -- it always invokes `solve-field` directly through
the wrapper, so this is safe to leave disabled permanently). If you've
already ruled that out and it's still refusing, the more general check
is Resource Monitor (`resmon`) -> CPU tab -> Associated Handles, search
for `cygwin1` -- that finds anything else system-wide still holding a
handle to Cygwin's core runtime, whatever it's called.

**`rebaseall` completed, but the same fork failure came right back.**
Most likely antivirus/EDR software injecting its own DLL into every new
process, which can reclaim the exact address Cygwin just freed before
the next fork even happens. Try adding an exclusion for
`%LOCALAPPDATA%\cygwin_ansvr` (or wherever ansvr is installed) in
whatever antivirus is active.

## Gaia tab: Python interpreter FAQ

`EpochFrom-gui`'s Gaia tab shells out to `scripts/gaia_field_query.py`
(see README's "Gaia data" section) via whatever's in its "Python
interpreter" field, using `QProcess` -- same mechanism, same
"single-program-path, not a shell command line" constraint, as
`solveFieldPath` above. This is unrelated to ansvr/Cygwin; it's a
completely separate external tool with its own setup.

**What goes in the "Python interpreter" field?** The path to a Python 3
executable that has `astropy` and `astroquery` installed. On Windows the
field defaults to `python` (see `GaiaTab.cpp`), which works as-is if you
have a stock python.org install with "Add python.exe to PATH" checked
during setup. If not, or if you're managing Python some other way,
you need the full path to a specific `python.exe` there instead.

**Using [`uv`](https://docs.astral.sh/uv/) to manage Python?** A
`uv`-installed standalone Python is a *directory*
(`%APPDATA%\uv\python\`), not a single executable -- pointing the field
at that directory itself fails (`QProcess` needs one concrete `.exe`).
The actual interpreter is one level deeper, inside a version-specific
subfolder, e.g.:

```
C:\Users\<you>\AppData\Roaming\uv\python\cpython-3.11.12-windows-x86_64-none\python.exe
```

Run `uv python find` in a terminal to print the exact current path
rather than guessing the version-folder name by hand.

**Installing `astropy`/`astroquery` into a `uv`-managed interpreter
fails with "The interpreter ... is externally managed".** `uv` blocks
installing packages directly into its own managed interpreters on
purpose, so other `uv` tools/projects sharing that interpreter don't get
their packages clobbered. Its own suggested fix is the right one -- make
a dedicated virtual environment instead:

```
uv venv epochfrom-gaia-env --python 3.11.12
uv pip install --python epochfrom-gaia-env\Scripts\python.exe astropy astroquery
```

(the `--python 3.11.12` pins it to a specific already-installed version
via `uv`'s own resolution -- omit it to let `uv venv` pick a default).
Note the path shape changes here: a `uv`-managed standalone install has
`python.exe` directly inside its version folder, but a venv puts it
under a `Scripts\` subfolder instead -- an ordinary Windows-venv detail,
not a `uv` quirk. Point the Gaia tab's "Python interpreter" field at the
venv's own `python.exe` (e.g.
`C:\Users\<you>\epochfrom-gaia-env\Scripts\python.exe`), not the
managed interpreter you started from -- that gives you a self-contained
environment with `astropy`/`astroquery` installed, isolated from
anything else `uv` manages.

**"Couldn't start '...' -- check the Python interpreter path."** Exactly
what it says -- `QProcess` couldn't launch whatever's in that field at
all. Most often this means the path doesn't exist, is a directory rather
than an executable (the `uv` case above), or a bare `python`/`python3`
isn't actually on `PATH` the way the field's default assumes. Doesn't
mean `astropy`/`astroquery` are missing -- that would fail differently,
after the interpreter successfully starts (an `ImportError`/
`ModuleNotFoundError` traceback in the Gaia tab's own log output,
naming the missing package), so if you see that instead, it's the pip
install into that specific interpreter that needs doing, not the path
itself.

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
