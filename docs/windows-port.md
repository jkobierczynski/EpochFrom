# Windows port: current status

EpochFrom has been built and used on Linux for most of its life. This
document tracks the Windows port: what changed to make one possible, and
the real findings from actually getting it building.

**The MSYS2/MinGW toolchain now builds EpochFrom end-to-end on Windows,
confirmed on a real machine, not just configured.** Getting there
surfaced a long chain of real, confirmed issues -- in wcslib's own build
system, in the interaction between wcslib's public header and MinGW's
own headers, and one genuine (if minor) portability bug in EpochFrom's
own code -- each detailed in the sections below as they were hit and
fixed. The short version, if you're setting this up fresh: follow
[MSYS2 + MinGW](#msys2--mingw) start to finish, including its wcslib
build steps, rather than assuming wcslib is packaged for you the way
Qt6/Eigen3/cfitsio are. The MSVC/vcpkg path was also attempted first and
is now understood to be a dead end for wcslib specifically -- vcpkg has
no Windows port for it and neither does conda-forge (both confirmed --
see [MSVC + vcpkg](#msvc--vcpkg)) -- so MSYS2/MinGW is the only path
confirmed to actually work, and is what the rest of this document
assumes. The `.rc` icon resources remain unexercised against a real
Windows toolchain, and the GUI/CLI executables haven't been separately
run yet even though the library build succeeded -- see
[What to actually do first](#what-to-actually-do-first). The test suite
itself has now been run and is fully green (9/9), including
[a real wcslib 8.9 regression](#test-suite-fits_header_update_test-sip-failure-root-caused-and-fixed)
found and fixed along the way.

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
          mingw-w64-x86_64-cfitsio mingw-w64-x86_64-cmake \
          mingw-w64-x86_64-pkgconf mingw-w64-x86_64-toolchain
```

**wcslib is not packaged in MSYS2**, confirmed, and there's no shortcut
around building it: conda-forge's `wcslib` feedstock was also checked as
a possible prebuilt alternative (the machine already has Anaconda) and
it explicitly skips Windows (`skip: true  # [win]` in its recipe;
`anaconda.org/conda-forge/wcslib` lists only linux-64/linux-aarch64/
osx-64/osx-arm64 builds) -- conda is a dead end here, not just untried.

Building wcslib from source is still the way, and it's more tractable
than it sounds: the wcslib 8.9 source tree ships its own
`wcslib.pc.in` (confirmed by inspecting it directly), so a stock
autotools build already produces a `wcslib.pc` -- no custom packaging
needed, just getting it into a prefix pkg-config searches. From an
**MSYS2 MinGW64 shell** (not the plain MSYS2 shell -- that matters, it's
what selects the `/mingw64` prefix and its gcc):

```
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake \
          mingw-w64-x86_64-pkgconf mingw-w64-x86_64-qt6-base \
          mingw-w64-x86_64-eigen3 mingw-w64-x86_64-cfitsio

cd /c/Users/<you>/Documents/wcslib-8.9   # or wherever the source lives
CPPFLAGS="-DNO_OLDNAMES" ./configure --prefix=/mingw64 --disable-fortran \
          --without-pgplot --disable-utils --disable-shared
make -j
make install
```

Every non-default flag above is explained below -- all four are required for
a clean `make install` under MinGW, not just style choices.

`--disable-fortran`/`--without-pgplot` skip wcslib's optional Fortran
bindings and PGSBOX plotting support (neither of which EpochFrom uses)
so the build doesn't need a Fortran compiler or the PGPLOT graphics
library at all -- both are auto-detected and gracefully skipped if
omitted anyway, but disabling them explicitly keeps a first attempt
fast and its failure surface small.

**Confirmed on the first real attempt:** stock wcslib 8.9's `configure`
mis-detects its thread-local-storage specifier under MSYS2/MinGW. It
probes for MSVC's `__declspec(thread)` first, and MinGW-w64's gcc
happens to accept that as valid C at *compile* time too (it supports
MSVC `__declspec` attributes for source compatibility) even though gcc
isn't MSVC -- so the probe "succeeds" and `configure` picks
`-DWCSLIB_TLS=__declspec(thread)` for every subsequent compile. That
string then reaches the shell unquoted, and `(`/`)` are shell
metacharacters, so *every* compile in the build -- not just `utils/`,
which is just where it's most visible since the library in `C/` may
scroll by before it -- fails with `syntax error near unexpected token
'('`. The fix is a small patch to the generated `configure` script that
removes the `__declspec(thread)` probe entirely, falling through
straight to the (correct, for gcc) `__thread` probe; already applied to
the copy in this project's history and delivered back to
`wcslib-8.9/configure` in place. If `./configure` still shows this
error, the patch didn't take (re-fetch it) or a stale `configure` from
before the patch is being invoked (check for a second copy or a leftover
`config.cache`); if the file lost its executable bit on the way through
Windows, run it explicitly as `sh ./configure ...` rather than
`./configure ...`. Either way, re-run `configure` and a clean `make`
afterwards -- the bad `-DWCSLIB_TLS` value is baked into the generated
Makefiles by `configure`, so just retrying `make` without reconfiguring
repeats the same failure.

**Second confirmed issue, hit right after the TLS one:** wcslib's own
`int wcsset(struct wcsprm *wcs)` (declared in `wcs.h`, defined in
`wcs.c`) collides with a completely unrelated `wcsset` that MinGW-w64's
`string.h`/`wchar.h` declares for MSVC compatibility (`wchar_t
*wcsset(wchar_t *, wchar_t)`, a legacy "fill a wide string" function,
deprecated since VS2005) -- same global name, unrelated signatures, so
gcc rejects it as a conflicting redeclaration and every file that
includes `wcs.h` (`wcs.c`, `wcsbth.c`, `wcsfix.c`, `wcshdr.c`,
`wcspih.c`, ...) fails to compile. MinGW-w64 anticipated this whole
class of collision: `wcsset` and its siblings (`strset`, `strlwr`,
`stricmp`, ...) are gated behind `#ifndef NO_OLDNAMES` in its headers
specifically so libraries that legitimately want those names can
suppress Microsoft's legacy ones. Defining `NO_OLDNAMES` (folded into
the `CPPFLAGS` in the command above) makes MinGW skip declaring them,
which is the standard fix for this exact clash and needs no changes to
wcslib's own source. A follow-on symptom from the same underlying
failure, seen in the same run: `install: target
'/mingw64/include/wcslib-8.9' is not a directory` -- that's not a third
bug, it's `C/`'s `make install` (which normally creates that directory
and populates it) never having run because the library build in `C/`
failed first; it resolves on its own once `wcsset` stops colliding and
`C/` actually builds. Because of that dependency, a first attempt can
leave a partial install behind (e.g. `tofits`/`fitshdr`/`sundazel` and
`wcslib.pc` under `/mingw64` from `utils/`'s install succeeding while
`C/`'s failed) -- harmless, a clean reconfigure-and-rebuild overwrites
it.

**Third confirmed issue:** the `NO_OLDNAMES` fix above has a collateral
cost -- it doesn't just suppress `wcsset`, it suppresses the whole
"oldnames" family, which also includes the no-underscore POSIX-style
aliases MinGW-w64 provides for its own CRT functions: `access`, `stat`
(and the `off_t` type that goes with it), `unlink`, and others, all
gated behind the same `#ifndef NO_OLDNAMES`. wcslib's own
`utils/fitshdr.c` (one of the optional command-line utilities, not part
of `libwcs` itself) uses exactly those names and fails to compile once
they're suppressed. EpochFrom doesn't use or need `tofits`/`fitshdr`/
`sundazel` at all, so `--disable-utils` (which removes `utils/` from
the build entirely -- confirmed against `configure.ac`'s
`SUBDIRS`/`INSTDIR` handling for that flag) sidesteps it rather than
patching `fitshdr.c` to use the underscored names.

**Fourth confirmed issue:** `make install` fails again afterwards, on
`C/`'s shared-library (DLL) install step, with `cp: cannot stat
'libwcs.dll.8': No such file or directory`. wcslib installs its shared
library under three chained names to mimic Unix `.so` SONAME
versioning -- `libwcs.dll.8.9` (the real file) -> `libwcs.dll.8` ->
`libwcs.dll` -- normally via symlinks, where a bare relative name in
the second link is fine because it resolves relative to the
destination directory when followed. On this platform `$(LN_S)`
(confirmed by reading the actual generated `C/GNUmakefile`) resolves to
a real `cp`, not `ln -s`, and a plain `cp` resolves that same bare name
against the current directory (`C/`) instead, where it was never
created -- an upstream bug in how the Windows/MinGW case was retrofitted
onto that Unix-style chain, not anything specific to this project.
Since EpochFrom only needs something `-lwcs`-linkable, not a versioned
DLL, `--disable-shared` (producing a static `libwcs.a` instead) avoids
the bug entirely and is simpler for a first working build regardless --
no DLL to keep on `PATH` at runtime. `wcslib.pc`'s `Libs: -lwcs` line
works unchanged either way; the linker just picks whichever `libwcs.*`
is actually present.

`make install` drops `wcslib.pc` into `<prefix>/lib/pkgconfig`, `wcs.h`
under `<prefix>/include/wcslib`, and `libwcs.*` under `<prefix>/lib`.
On this build's actual first attempt, `<prefix>` ended up being a
dedicated directory under the user's own `Documents` (e.g.
`/c/Users/<you>/Documents/wcslib-mingw64`) rather than the shared
`/mingw64` pacman uses, because `install -d -m 775` kept failing with
"cannot change permissions" (a `chmod` failure, not a missing-directory
one -- the directory gets created regardless, so it's mostly harmless
where the Makefile already tolerates it, and only actually blocks
where a given install rule doesn't) even after moving off of a
QMK-Firmware-bundled MSYS2 install (`C:\QMK_MSYS`) onto a location the
user fully owns, meaning it's a general MSYS2-on-this-Windows-install
behaviour, not specific to that one directory. Whichever prefix wcslib
ends up in, pkg-config needs to be told about it explicitly if it isn't
`/mingw64` (which pacman's own packages -- Qt6, Eigen3, cfitsio -- are
still expected to live in):

```
export PKG_CONFIG_PATH="<wcslib prefix>/lib/pkgconfig:$PKG_CONFIG_PATH"
```

**Fifth confirmed issue:** `make install` fails a third time, on both
`C/`'s and the top-level `GNUmakefile`'s alias step, with `cp: cannot
stat 'wcslib-8.9': No such file or directory` -- the same underlying
cause as the DLL-chain bug above (`$(LN_S)` resolving to a plain `cp`,
not `ln -s`, on this system), hitting a second spot: `C/GNUmakefile`
aliases the versioned include directory (`wcslib-8.9`) to the bare name
(`wcslib`) consumers actually `#include`, and the top-level
`GNUmakefile` does the same for the doc directory, both via
`$(LN_S) $(notdir $(INCDIR)) $(INCLINK)` / `$(LN_S) $(notdir $(DOCDIR))
$(DOCLINK)`. `$(notdir ...)` strips the path down to a bare relative
name, which is exactly right for a real symlink (resolved from the
destination directory) but wrong for a plain `cp`, which resolves that
same bare name against the current directory instead, where it was
never created. Fixed by patching both Makefiles directly to pass the
full path instead of the bare name:

```
sed -i 's|$(LN_S) $(notdir $(INCDIR)) $(INCLINK)|$(LN_S) $(INCDIR) $(INCLINK)|' /c/Users/<you>/Documents/wcslib-8.9/C/GNUmakefile
sed -i 's|$(LN_S) $(notdir $(DOCDIR)) $(DOCLINK)|$(LN_S) $(DOCDIR) $(DOCLINK)|' /c/Users/<you>/Documents/wcslib-8.9/GNUmakefile
```

(applied via `sed` rather than delivered as a file -- the file-delivery
tooling used through this whole port refuses to write anything literally
named `GNUmakefile`.) With that, `make install` completes.

**Sixth confirmed issue, only visible on a *second* `make install`:**
after `dis.c` was patched for the SIP bug below and wcslib rebuilt,
`make install` failed again, this time with `cp: cannot create regular
file '.../wcslib/wcshdr.h': Permission denied` (and the same for every
other header). Cause: the fifth issue's fix made the alias step *find*
the right destination, but `cp -pR` (still standing in for `ln -s`) is
not idempotent the way a real symlink would be -- the first successful
install already left real copies of every header there, `install -m
444`'d (read-only), and a second `cp -pR` run tries to overwrite those
existing read-only files by opening them for writing rather than
unlinking and recreating them, which fails. This will recur on *every*
future `make install` after the first, not just this one. Fixed the
same way, adding `-f` (force: "if an existing destination file cannot be
opened, remove it and try again") to both `cp` invocations:

```
sed -i 's|$(LN_S) $(INCDIR) $(INCLINK)|$(LN_S) -f $(INCDIR) $(INCLINK)|' /c/Users/<you>/Documents/wcslib-8.9/C/GNUmakefile
sed -i 's|$(LN_S) $(DOCDIR) $(DOCLINK)|$(LN_S) -f $(DOCDIR) $(DOCLINK)|' /c/Users/<you>/Documents/wcslib-8.9/GNUmakefile
```

From there, configuring EpochFrom itself needs no
`CMAKE_PREFIX_PATH`/`WCSLIB_ROOT` for the wcslib/cfitsio detection
itself -- it's the exact pkg-config branch already proven on Linux --
though it does need one small addition to `CMakeLists.txt` itself, see
below:

```
cmake -B build-mingw -S . -G "MSYS Makefiles"
cmake --build build-mingw
```

**Confirmed on the first real attempt:** this doc originally suggested
`-G "MinGW Makefiles"`, which fails its own compiler self-test when
run from an MSYS2 bash prompt, with a mangled-looking error like
`'mTC_7e1e9.dir' is not recognized as an internal or external
command`. That's a real, documented incompatibility, not a fluke --
CMake's own docs for that generator say outright "They are not
compatible with MSYS or a unix shell," which is exactly the shell this
whole toolchain runs from. `-G "MSYS Makefiles"` is the generator
variant made for that case ("in a MSYS shell prompt and using `make` as
the build tool," per its own docs); `-G Ninja` (if
`mingw-w64-x86_64-ninja` is installed) sidesteps the distinction
entirely and works from either shell, if preferred instead. Delete any
`build-mingw` directory left over from a `-G "MinGW Makefiles"` attempt
before reconfiguring -- it caches that generator's failed compiler-test
results and won't self-correct on its own.

**Confirmed on the first real attempt, once the build actually started
compiling:** the `wcsset` collision that wcslib's own build works
around for itself (see the `NO_OLDNAMES` finding above) resurfaces in
EpochFrom's own source, because it doesn't inherit wcslib's build
flags. `wcs.h`'s public, unconditional `int wcsset(struct wcsprm
*wcs);` declaration collides with MinGW's own `wcsset` the moment any
translation unit includes both -- and essentially every one that
includes `wcs.h` does, transitively, because Qt headers pull in
`<string.h>` too (confirmed: it surfaced compiling `PlateSolver.cpp`,
via `wcshdr.h` on one side and `QMetaType` -> `qarraydata.h` on the
other). First fix attempt defined `NO_OLDNAMES` project-wide (top-level
`CMakeLists.txt`, `if(MINGW)`) -- also confirmed wrong on the same
build: it broke `GaiaCatalog.cpp`, a file with nothing to do with
wcslib, with an unrelated `'pid_t' was not declared in this scope`
error, because `NO_OLDNAMES` also gates `pid_t` and other MinGW-w64
typedefs that GCC's own C++ standard library needs internally (pulled
in transitively via winpthread's `sched.h`, itself pulled in by Qt
headers) -- so a genuinely project-wide define breaks anything that
doesn't touch wcslib at all. Second attempt scoped the same
`NO_OLDNAMES` define down to just the three files confirmed (by
grepping the directory) to actually include a wcslib header --
`Wcs.cpp`, `LinearWcs.cpp`, `PlateSolver.cpp` -- via
`set_source_files_properties`. Also confirmed wrong, on the very same
files: each of those three *also* pulls in Qt's own
`<pthread.h>`/`<sched.h>` chain (same root cause as `GaiaCatalog.cpp`
above), and a `-D` compiler flag is active for a file's *entire*
compilation from its first character, so there's no way to have
`NO_OLDNAMES` "on" for wcs.h's declaration but "off" moments earlier
for Qt's -- both collisions are triggered by the same early Qt
`#include` chain, in the same file, so the macro can't be scoped
in-file to dodge one without the other.

**Correction, found while setting up CI for this project (see
[Release workflow](#release-workflow) below):** the fix actually in
place today is different from, and simpler than, what this section
used to describe here -- that earlier plan (each of the three files
declaring its own `epochfrom_wcsset` alias and calling that instead of
plain `wcsset()`) was superseded before it shipped. `PlateSolver.cpp`
genuinely calls plain `wcsset(wcs)` (confirmed directly in the current
source), which only works because the fix was moved to a single place:
wcslib's own installed `wcs.h`, patched once, right where it declares
`wcsset()`, rather than at every call site:

```c
#if defined(__MINGW32__) || defined(__MINGW64__)
extern int wcs_wcsset(struct wcsprm *wcs) asm("wcsset");
#define wcsset wcs_wcsset
#else
int wcsset(struct wcsprm *wcs);
#endif
```

(confirmed by diffing the actual installed header against wcslib's own
pristine source -- this exact block, byte-for-byte, is the only
difference). This sidesteps the name clash exactly the same way as the
abandoned per-file approach -- MinGW's own `wcsset` declaration is left
alone and unused, and nothing needs `NO_OLDNAMES` or any macro scoping
to avoid the `pid_t` collateral damage -- but needs applying only once,
to wcslib's header, rather than at every EpochFrom call site, and
doesn't leave `PlateSolver.cpp`/`Wcs.cpp`/`LinearWcs.cpp` calling a
differently-named wrapper. `scripts/ci/patch_wcslib_wcs_h.py` applies
this same patch automatically, idempotently, to a freshly-fetched
wcslib source as part of [the release workflow](#release-workflow) --
confirmed to reproduce the real installed header byte-for-byte when
run against a reconstructed pristine copy.

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

cfitsio's vcpkg port existing is good news, and this is now confirmed
rather than guessed: a real MSVC/vcpkg configure got `CFITSIO_DIR` set
correctly (so `find_package(CFITSIO CONFIG QUIET)` did locate the
package), but the `TARGET cfitsio` / `TARGET CFITSIO::CFITSIO` checks
this pass originally guessed at both failed -- the actual vcpkg port in
use (cfitsio 4.7.0) exports `CFITSIO::cfitsio` (namespace `CFITSIO::`,
target `cfitsio`, i.e. mixed case), traced to upstream cfitsio's own
`install(EXPORT cfitsioTargets NAMESPACE CFITSIO:: ...)` call.
`CMakeLists.txt`'s cfitsio-detection block now checks for that exact
target first, with the other two kept as fallbacks.

**wcslib has no usable vcpkg port on Windows.** vcpkg does carry a
`wcslib` port now, but checking it directly shows its `vcpkg.json`
declares `"supports": "!windows"` -- it wraps wcslib's autotools build
via `vcpkg-make`, which isn't portable to MSVC, so it's excluded from
the `x64-windows`/`x64-windows-release` triplets outright. Adding it to
`vcpkg.json` would just be silently skipped for a Windows build, not
install anything. conda-forge's `wcslib` package was checked too, as a
possible prebuilt escape hatch -- also a dead end, it skips Windows
outright (see [MSYS2 + MinGW](#msys2--mingw) for the confirmation and
the receipt). On MSVC, the only path left is building wcslib manually
against MSVC itself -- nothing here has attempted that yet, it would
mean either translating its autotools build to MSVC by hand or running
its `configure`/`make` under something like Cygwin while still
producing MSVC-compatible import libs -- and then pointing at the
result via:

```
cmake -B build -S . -DCMAKE_PREFIX_PATH=<wcslib install prefix> ...
```

`cmake/FindWCSLIB.cmake` picks it up from there -- see that file for the
exact search hints (`WCSLIB_ROOT` also works, as an alternative to folding
it into `CMAKE_PREFIX_PATH`). Given that cost, **switching to MSYS2/MinGW
for wcslib specifically (or for the whole build) is the recommended
next step instead** -- see [MSYS2 + MinGW](#msys2--mingw), where it
builds with its own stock recipe and no MSVC-porting work at all.

## Plate-solving: ANSVR

`solve-field` (astrometry.net) has no native Windows build. EpochFrom
doesn't attempt to work around this -- `PlateSolver`'s `solveFieldPath`
option (the Solve tab's "solve-field path" field, or `--solve-field-path`
on the CLI) already just takes whatever's on `PATH` or an explicit path,
so the intended route is [ANSVR](http://ronallo.com/astrometry-net-ansvr)
(or a current equivalent), the local astrometry.net solver already used
by a lot of the PixInsight-on-Windows community, pointed at from that
setting.

**Confirmed on a real ANSVR install, first attempt:** the GUI's
"solve-field path" field doesn't auto-detect ANSVR's install location
(nothing in `PlateSolver` looks for it yet -- a small follow-up worth
doing, not done here), so it needs pointing at
`.../cygwin_ansvr/lib/astrometry/bin/solve-field.exe` by hand. Once
that's set, the first real solve attempt failed with EpochFrom's own
generic `"solve-field did not solve the field (no .solved/.wcs output
produced)"` -- notably with *no* solve-field output at all, which is
itself the tell (even a genuine failed solve normally prints plenty of
diagnostic text). Confirmed by running the exact same `solve-field.exe`
invocation directly from a plain `cmd.exe` (bypassing EpochFrom
entirely, to match exactly what `QProcess::start()` does with no shell
involved): Windows' own error dialog, *"The code execution cannot
proceed because cygwin1.dll was not found."* `solve-field.exe` is a
Cygwin binary, and unlike a normal DLL dependency, Cygwin's runtime DLL
isn't colocated with it -- on this install it's at
`.../cygwin_ansvr/bin/cygwin1.dll`, three directories up from
`solve-field.exe`'s own `.../cygwin_ansvr/lib/astrometry/bin/`. ANSVR's
own tray app/scheduler presumably puts that directory on `PATH` before
ever launching `solve-field.exe` itself; a plain `QProcess::start()`,
with none of that surrounding environment, doesn't, so it fails the
same way every time when launched from EpochFrom.

**Fixed** in `PlateSolver::solve()`: a new `findCygwinRuntimeDir()`
helper searches upward from the configured `solve-field.exe`'s own
directory for a sibling `cygwin1.dll` (checking that directory itself
and its `bin/` subdirectory, up to 6 levels), and if found, a new
`solveFieldEnvironment()` prepends it to the child process's `PATH` via
`QProcessEnvironment` before `proc.start()` -- rather than hardcoding
ANSVR's exact directory layout (which could change) or asking the user
to edit their system `PATH` by hand. Harmless no-op for a solve-field
build that isn't a Cygwin binary at all (nothing found nearby).

**Confirmed fixed, but surfaced a second, real, separate issue:**
solve-field now actually launches. Doing so, though, printed its own
full `--help` text and refused to run, with `solve-field: unknown
option -- temp-axy`. The `solve-field.exe` this particular ANSVR
install bundles turns out to be a build from **2010** (its own banner:
Subversion revision 16745, tarball-0.38) -- old enough to predate
`--temp-axy` entirely, and this version treats an unrecognized flag as
fatal rather than ignoring it. (`--new-fits`/`--rdls`/`--match`/`--corr`
with `none`, also used to suppress byproducts, remain fine -- confirmed
present in this exact version's own printed option list, along with its
explicit note that most output files accept `none`.)

**Fixed:** a new `solveFieldSupportsFlag()` helper runs `solve-field
--help` once per solve-field path (cached, since a directory batch or
repeated GUI solves would otherwise re-probe every time) and checks its
output for a given flag string before relying on it; `--temp-axy` is
now only added to the argument list if that probe finds it. Without
`--temp-axy`, an old install like this one simply leaves the `.axy`
byproduct behind at `<base>.axy` -- a known, harmless limitation of that
particular install, not worth failing the whole solve over the way an
unconditional `--temp-axy` would. (The help-probe subprocess needed the
same `solveFieldEnvironment()` PATH fix as the real solve -- it launches
`solve-field.exe` too, and would otherwise hit the identical missing-DLL
failure and silently report every flag as unsupported.)

**Confirmed fixed and rebuilt, but surfaced a third, real, separate
issue:** after rebuilding with both fixes above, the exact same
generic `"solve-field did not solve the field (no .solved/.wcs output
produced)"` failure -- with empty stdout/stderr -- came back. The
first diagnostic step taken here was actually a mistake worth
recording: re-running the same `solve-field.exe` invocation directly
from `cmd.exe` (as done for the original cygwin1.dll finding above) to
isolate the cause. That technique doesn't apply to this fix the way it
did to the first one -- `solveFieldEnvironment()` only modifies the
*environment of the child process EpochFrom itself launches* via
`QProcess`/`QProcessEnvironment`; it never touches the system-wide
`PATH` or any manually-typed shell's own environment, so a `cmd.exe`
repro can never reflect it either way, fix present or not. (No reboot
needed for ANSVR either, for the same reason -- nothing about this fix
depends on system-wide state that a reboot would refresh.) The real
question was whether the *GUI's own* Solve button, not a hand-typed
command, still hit the cygwin1.dll dialog, and it did.

A screenshot of the GUI's "solve-field path" field at that point showed
the answer: the field's contents were `"C:\Users\PC\AppData\Local\
cygwin_ansvr\lib\astrometry\bin\solve-field.exe"`, wrapped in literal
double-quote characters -- visibly different from the unquoted path in
an earlier screenshot. Most likely cause: this project's own docs (and
this conversation's `cmd.exe` repro suggestions) show the path
pre-quoted for a command line, and it's the natural thing to
copy-paste straight into the GUI's plain-text field, which has no
reason to strip it back out. Windows' own `CreateProcess` tolerates a
quoted image name in the command line just fine, so `solve-field.exe`
still launched -- but every `QFileInfo`-based check in `PlateSolver.cpp`
(`findCygwinRuntimeDir()`'s search chief among them) treats the quote
characters as literal path content rather than a delimiter, so `.dir()`
computes a directory that doesn't exist, and the cygwin1.dll search
silently finds nothing. The PATH fix never actually applied, and the
old failure mode resurfaced, even though the fix itself was correct.

**Fixed:** a new `cleanExecutablePath()` helper trims whitespace and
strips one matching pair of surrounding double quotes; `PlateSolver::
solve()` now computes `solveFieldPath = cleanExecutablePath(options.
solveFieldPath)` once, near the top, and uses that cleaned value
everywhere in the function that a path is checked or the process is
launched (`findCygwinRuntimeDir`/`solveFieldEnvironment`/
`solveFieldSupportsFlag`, `proc.setProgram()`, the `setsid`-wrapped
argument list, and the "failed to start" error message), rather than
requiring the path field to be pasted in unquoted.

**Confirmed: all three fixes above work.** After rebuilding and
retrying from the GUI, the failure changed character completely --
instead of the earlier silent, empty-output "did not solve" result,
solve-field now prints substantial real diagnostic output for the
first time, starting with `Reading input file 1 of 1: "C:/Users/PC/
Pictures/astro/.../m51_Light_clear_240_secs_003.fits"...`. That's solid
evidence solve-field is now launching cleanly, resolving cygwin1.dll,
accepting its argument list (no `--temp-axy` rejection), and reading
the actual configured path correctly (no stray quotes breaking
anything) -- progress moved from "never starts" to "starts and reaches
its own source-extraction stage," a different part of the pipeline
entirely. That stage then hit a fourth, real, separate issue:

```
8 [main] python2.7 37056 child_info_fork::abort: address space needed
by 'cygcrypto-1.0.0.dll' (0x3520000) is already occupied
...
OSError: [Errno 11] Resource temporarily unavailable
augment-xylist.c:585:backtick Failed to run command:
/usr/lib/astrometry/bin/image2pnm.py ...
```

This is **not an EpochFrom code issue** -- it's a well-documented,
long-standing Cygwin problem, unrelated to anything `PlateSolver.cpp`
does. `solve-field` shells out to `image2pnm.py` (bundled Python 2.7,
itself a Cygwin binary) to convert/sanitize the input image, which in
turn `fork()`s further helper processes; Cygwin's `fork()` emulation on
Windows requires every DLL in the child to load at the exact same base
address recorded in Cygwin's own "rebase" database, and if that
address is already occupied by something else in the new process's
address space (stale rebase data after packages were added/updated,
ASLR variance, antivirus DLL injection, etc.), the load -- and the
`fork()` -- fails outright with exactly this `child_info_fork::abort`
message and Python surfacing it as `OSError: [Errno 11] Resource
temporarily unavailable`. This is a known-quantity failure mode
documented on Cygwin's own mailing list and elsewhere (see Sources
below), with a standard fix: **`rebaseall`**, which recomputes
non-conflicting base addresses for every DLL in the installation.

**Recommended fix (install-level, not a code change):**
1. Close every process that has ANSVR's `cygwin1.dll` loaded --
   the ANSVR tray icon/indexer service if it's running, any open
   ANSVR/cygwin shell windows, and EpochFrom itself while doing this.
2. Open an ordinary Windows Command Prompt **as Administrator**
   (rebaseall must run from a plain `cmd.exe`/`ash`, not from inside a
   bash/cygwin shell, since it can't rebase DLLs a running Cygwin
   process still has mapped).
3. Run:
   ```
   C:\Users\PC\AppData\Local\cygwin_ansvr\bin\ash.exe /bin/rebaseall -v
   ```
   (ANSVR's own bundled `ash.exe` and `rebaseall`, at the same
   `.../cygwin_ansvr/bin/` that holds `cygwin1.dll` -- confirmed
   present in a standard ANSVR layout.)
4. Let it finish (prints one line per DLL rebased), then retry the
   same solve from the GUI.

If that alone doesn't clear it, the next most common contributing
cause is antivirus real-time protection injecting itself into the
process and shifting DLL addresses unpredictably -- worth testing with
a temporary exclusion for the `cygwin_ansvr` folder (or briefly
disabling real-time scanning) to see if that's a factor here, though
`rebaseall` is the standard fix and should be tried first.

**`rebaseall` confirmed the fix (progress moved further, not resolved
outright), surfacing a fourth, real, separate issue.** After running it,
the fork-abort/cygcrypto failure was gone, and solve-field got further
still -- past reading the image and into calling astrometry.net's own
`image2pnm.py` (bundled Python 2.7) to preprocess it, which then failed
with:

```
File ".../pyfits/__init__.py", line 48, in <module>
    raise ImportError, `e` + ".  No usable array package has been found. ..."
ImportError: ImportError("ImportError('No module named numarray',). ...
```

Diagnosed by reproducing this standalone, entirely outside EpochFrom
(running `image2pnm.py` directly via this install's own `python2.7.exe`,
with the same cygwin1.dll-directory PATH addition our fix already
makes) -- confirming it's not an artifact of how EpochFrom launches
things, but a real, install-level Python problem. That old pyfits
version's own exception handling turned out to be misleading: it
catches the `import numpy` failure, then also fails to `import
numarray`, and raises a new error that only ever includes the
*numarray* failure text, silently discarding numpy's real one. A
small standalone diagnostic script (bypassing pyfits, importing numpy
directly and printing its actual traceback) surfaced the real error:

```
File ".../numpy/core/__init__.py", line 14, in <module>
    from . import multiarray
ImportError: No such file or directory
```

`multiarray.dll` (numpy's compiled core) was confirmed to actually
exist at its expected path (not missing/corrupted), so this is the
same *class* of problem as the cygwin1.dll one -- a DLL that exists but
can't resolve one of its own dependencies, reported uninformatively.
Cygwin's own `cygcheck` utility (bundled with this install) pinpointed
the actual missing dependency: `cygblas-0.dll` (Cygwin's BLAS library,
needed by numpy's linear-algebra routines) -- confirmed present in the
install, but at `.../cygwin_ansvr/lib/lapack/`, a completely different
directory from `cygwin1.dll`'s own `.../cygwin_ansvr/bin/`.

**Fixed, generally this time rather than one more special case:**
`findCygwinRuntimeDir()` was replaced with `findCygwinLocation()`,
which locates both cygwin1.dll's own directory *and* the install's
apparent root directory, and a new `collectDllDirectories()` walks
that whole install tree (breadth-first, capped at 6 levels deep) and
collects *every* directory containing at least one `*.dll` -- not just
the one containing cygwin1.dll. `solveFieldEnvironment()` prepends all
of them to PATH. Rather than hardcode `lib/lapack` as a second special
case (and inevitably hit a *third* some pipeline stage later), this
covers any DLL dependency anywhere in the install, present or future,
the same way ANSVR's own launcher environment apparently already does.
Cached per install root, since walking the filesystem isn't free and
every solve (and the `--help` probe) needs the identical environment.

**Confirmed working end-to-end, but surfaced one more transient issue on
the very next attempt.** After rebuilding with all four fixes above, a
real solve succeeded completely for the first time -- correct RA/Dec,
field size, pixel scale, and a written `.wcs` sidecar, all against a
real M51 LRGB frame, with EpochFrom's directory-mode pointing-hint/
pixel-scale prefill also confirmed working from that result. The very
next attempt (same install, no config change) hit the *original*
`child_info_fork::abort`/cygcrypto-1.0.0.dll fork failure again --
briefly looking like a regression from the DLL-directory-scan fix
above, but ruled out by evidence: `cygcrypto-1.0.0.dll` exists in
exactly one place in the install (`cygwin_ansvr/bin`, the same
directory already on PATH before that fix existed), so there's no
duplicate/shadowed copy involved. Simply retrying the identical solve,
completely unchanged, succeeded immediately. This matches a known,
documented limitation of Cygwin's `fork()` emulation on Windows: even
after `rebaseall` fixes the DLLs it manages, an unrelated DLL loaded at
a Windows-randomized address can still occasionally collide with one of
those precomputed slots on a given run, purely by chance -- rare,
transient, and not something any of the four fixes above could ever
fully eliminate on this vintage of Cygwin.

**Fixed:** rather than requiring the user to notice this specific
failure and click Solve again by hand, `PlateSolver::solve()` now
detects the `child_info_fork::abort` signature specifically in
solve-field's output and retries the whole solve-field invocation
automatically, up to 3 attempts total, before surfacing a failure to
the caller. Any other failure (a genuine no-match-found, a bad path, an
unsupported flag) is surfaced immediately on the first attempt, exactly
as before -- only this one well-understood, confirmed-transient
signature gets retried silently.

**ANSVR plate-solving is now confirmed working end-to-end on Windows,**
across all five issues found and fixed in this section: the cygwin1.dll
PATH gap, the old-version `--temp-axy` incompatibility, the quoted-path
regression, the cygblas-0.dll PATH gap (fixed generally via
`collectDllDirectories()`), and this transient fork-abort retry.

## Gaia field query: Python environment (confirmed working, install-level)

`scripts/gaia_field_query.py` (the other external process EpochFrom
shells out to, alongside `solve-field` -- see
[What already works without changes](#what-already-works-without-changes))
is a separate Python script, run via whatever interpreter is configured
in the GUI's Gaia tab (`Q_OS_WIN`-conditional default of `python`, always
editable -- see [What this pass changed](#what-this-pass-changed)). Its
own docstring already documents its two real dependencies: `pip install
astropy astroquery`.

**Confirmed on the same real Windows machine:** with the default
`python` left as-is, an actual Gaia query attempt from the GUI failed
immediately with `ModuleNotFoundError: No module named 'astropy'`.
Worth recording precisely which interpreter that was, since this
machine has more than one Python installed (Anaconda, confirmed earlier
in [MSYS2 + MinGW](#msys2--mingw) while checking for a prebuilt wcslib)
and bare `python` could plausibly have resolved to any of them:
`sys.executable` confirmed it as `C:/QMK_MSYS/mingw64/bin/python.exe`
3.12.11 -- the MinGW-packaged Python bundled with QMK MSYS (a
customized MSYS2 distribution the user has installed, unrelated to
EpochFrom's own MSYS2 MinGW64 build environment except for sharing the
general MSYS2 packaging conventions), not Anaconda's.

That specific Python build has no `pip` at all (`python -m pip`: "No
module named pip"), and `python -m ensurepip --upgrade` -- normally the
standard fallback when `pip` itself is missing -- surfaced a further,
real layer: PEP 668's "externally-managed-environment" guard, which
MSYS2 applies to its own system Python to stop `pip` from writing into
a package-manager-owned install directly. Its own error text already
names the standard way out for a package MSYS2 doesn't otherwise
package (`astroquery` in particular is a fairly niche VO/archive-query
library, not confirmed either way in MSYS2's repo, and not worth
chasing when the venv route sidesteps the question entirely): create a
virtual environment.

**Fixed, install-level (no EpochFrom code change needed):**

```
python -m venv C:/Users/PC/Documents/EpochFrom/gaia-venv
C:/Users/PC/Documents/EpochFrom/gaia-venv/bin/pip.exe install astropy astroquery
```

Confirmed this MSYS2-packaged Python lays out a venv POSIX-style
(`gaia-venv/bin/python.exe`, `.../bin/pip.exe`), not the `Scripts/`
layout a python.org or Store Python install would use -- worth checking
directly (`ls` the venv after creating it) rather than assuming either
way on a Python whose own packaging is already this unusual. With the
venv's packages installed, pointing the GUI's "python interpreter path"
field at `C:\Users\PC\Documents\EpochFrom\gaia-venv\bin\python.exe`
(instead of bare `python`) resolved it -- **confirmed working
end-to-end**, a real Gaia query against a real plate-solved field
completing successfully.

## Test suite: `fits_header_update_test` SIP failure (root-caused and fixed)

With the build itself working end-to-end, `ctest`/`cmake --build
build-mingw --target test` was run for the first time on this platform:
8 of 9 tests pass. The failure is `fits_header_update_test`'s Case 1 (a
synthetic astrometry.net-style `.wcs` sidecar carrying a SIP distortion
pair, order 2) -- Case 2 (identical header, no SIP terms) and Case 3
pass, isolating the problem specifically to SIP handling:

```
FAIL: readWcsFile on synthetic SIP .wcs didn't solve: wcslib wcsset()
failed (status 5): Invalid parameter value
```

Status 5 is `WCSERR_BAD_PARAM`. `PlateSolver::readWcsFile()` didn't have
wcslib's detailed per-struct error messaging (`wcserr`) turned on at all
before now, so the very first thing done was flip that on
(`wcserr_enable(1)`, plus surfacing `wcs->err->msg`) -- confirmed to
compile and run, but the message it captured is just the generic
boilerplate text for status 5, not a specific explanation. Tracing this
by hand through wcslib 8.9's own source (`wcs.c`/`lin.c`/`dis.c`) found
why: wcslib deliberately discards a lower-level module's detailed
message when remapping its status code across a module boundary --
`wcsset()` calls `linset()`, and on failure does
`wcserr_set(WCS_ERRMSG(wcs_linerr[status]))`, which replaces whatever
message `linset()` (or the `disset()`/`sipset()` it calls in turn, where
SIP's own decoding lives) actually set with the generic text for the
*remapped* top-level code. So the generic message is expected wcslib
behaviour, not a sign that our own diagnostics are broken -- it just
means `wcs->err->msg` alone can't point at the real cause here, and
`wcs->err->function`/`file`/`line_no` (also now surfaced) will likewise
only ever point at the `wcsset()`/`linset()` remapping site, not
wherever inside `dis.c` the rejection actually happened.

To get past that, `sipset()`/`disset()` in wcslib's own C source were
read directly and hand-traced against this test's exact header values
(`CTYPE1/2=RA---TAN-SIP/DEC--TAN-SIP`, `CRPIX=1.5/1.5`,
`CRVAL=314.925/43.664`, `CD1_1=-0.0002778` etc., `A_ORDER=B_ORDER=2`,
`A_0_0/A_1_1/A_2_0`, `B_0_0/B_1_1`, no inverse `AP_*`/`BP_*` terms) --
no check in that logic rejects this data. To confirm rather than just
trust the trace, that exact keyword/value set was hand-built into FITS
80-column card images and fed to a real wcslib build (via astropy's
vendored wcslib C source, close enough to 8.9 for this rarely-touched
legacy-compat code) in a throwaway Linux test program:
`wcspih()`+`wcsset()` **succeed** on that data, confirming the SIP
decoding logic itself isn't the problem -- something about how the
*actual* header text differs on this platform is.

One real, confirmed-live difference was found along the way and is worth
fixing regardless of whether it's the cause of this specific failure:
wcslib's own numeric parsing (`wcsutil_str2double()`) explicitly
localeconv()-adjusts the FITS '.' decimal point to whatever the
process's current `LC_NUMERIC` locale uses, precisely because plain
`sscanf("%lf", ...)` is locale-sensitive -- but a quick test (forcing
`nl_BE.UTF-8`/`de_DE.UTF-8`, both comma-decimal, onto the same Linux
repro) shows this can still silently drop every floating-point keycard
in a header (`wcspih()`'s `nreject` counter goes from 0 to 13 out of 13
float-valued cards) while `wcsset()` *still reports success* -- just with
those values defaulted to zero rather than an outright failure. That
doesn't match this test's actual symptom (a hard `wcsset()` failure, not
a silent-zero success), so it's very unlikely to be the direct cause
here, but a Qt GUI app not forcing `setlocale(LC_NUMERIC, "C")` (or
equivalent) at startup is a real, separate latent bug on any non-English
Windows locale -- Belgian Dutch/French included -- worth its own fix
later regardless of how this investigation concludes.

`PlateSolver::readWcsFile()` was changed to keep a copy of the *exact*
raw header text `fits_hdr2str()` handed to `wcspih()`, surface
`wcspih()`'s `nreject` count even when it otherwise "succeeds", and on a
`wcsset()` failure append that raw header text plus
`wcs->err->function`/`file`/`line_no` to the error message. Rerunning the
test with that in place gave real, byte-exact data to work from:

```
FAIL: readWcsFile on synthetic SIP .wcs didn't solve: wcslib wcsset()
failed (status 5): Invalid parameter value (in wcsset, wcs.c:2896)
header as read:
... CTYPE1  = 'RA---TAN-SIP' ... A_ORDER = 2 ... A_0_0 = 0. A_1_1 =
1.23E-06 A_2_0 = -4.5E-07 B_0_0 = 0. B_1_1 = -2.1E-06 END
```

`nreject` was 0 -- wcspih() parsed every card cleanly, ruling out the
locale/header-formatting theories above entirely. To close the loop, a
small C program was written that calls the *same* cfitsio
`fits_write_key()` sequence `writeSyntheticWcs(withSip=true)` uses, and
its output header was byte-for-byte identical (down to `nkeys=28` and
every card) to what came back from Windows -- so the header text was
never the issue; it's identical on both platforms.

That real header was then fed straight into a real wcslib build (as a
throwaway Linux test program, using astropy's vendored wcslib C source).
It **succeeded** -- so the SIP data itself, and the SIP-decoding logic as
last tested there, is fine. That pointed at a difference between wcslib
versions rather than platforms, so the actual **installed 8.9 source**
(`wcslib-8.9/C/dis.c`, staged directly off this machine) was read next,
and it does differ from the untested-here 8.6 baseline in exactly the
relevant function, `sipset()`:

```c
    } else if (degree[idis] == 9) {
      ncoeff[idis] = 60;
      distpd[idis] = tpd9;
    } else {
      // Can't happen; this is just to appease clang-tidy.
      return wcserr_set(WCSERR_SET(DISERR_BAD_PARAM),
        "Degree must not exceed 9, got %d", degree[idis]);
    }
```

That `else` branch doesn't exist in 8.6. **This is the actual bug**, and
it's in wcslib 8.9 itself, not in EpochFrom, not in this platform, and
not in the test's synthetic data: `sipset()` uses `degree[1] = -1` as its
own internal sentinel meaning "no REV (inverse) SIP terms were found in
the header at all" (SIP's inverse polynomial is optional -- our
synthetic test, like plenty of real astrometry.net output, supplies only
the forward `A_p_q`/`B_p_q` terms and no `AP_p_q`/`BP_p_q`). 8.9 added
this catch-all specifically to placate clang-tidy about an
unreachable-looking `else`, but it neglected that `degree[idis]` can
legitimately still be `-1` at that point for axis-inverse pairs with no
REV terms -- so instead of leaving `ncoeff[1] = 0`/`distpd[1] = 0x0` (the
correct, previously-working "no inverse computed" outcome), it now
rejects the whole distortion with `WCSERR_BAD_PARAM`. Confirmed by
patching a throwaway 8.6 copy to add back that exact `else` clause: the
same real header that had just succeeded now fails identically (status
5, function `wcsset`, generic "Invalid parameter value" message) --
matching the real Windows failure exactly, modulo the line number
difference expected between 8.6 and 8.9's `wcs.c`.

**Fix applied** to `wcslib-8.9/C/dis.c`'s `sipset()`: skip straight past
that `else` (leaving `ncoeff[idis]`/`distpd[idis]` at their safe
defaults) whenever `degree[idis] < 0`, before ever reaching the
9-degree-ceiling check -- the forward axis (`idis == 0`) can never hit
this since its degree is validated into `[1, 9]` (or defaults to `1`)
before this loop runs regardless, so the guard only ever changes
behaviour for the legitimate "no inverse present" case. Verified against
the same Linux repro: with the guard in place, the identical real header
that failed above now makes `wcsset()` succeed. Pushed to
`wcslib-8.9/C/dis.c` on this machine, then wcslib rebuilt and reinstalled
(`make` + `make install` from `wcslib-8.9` -- only `dis.c` changed, so a
full `./configure` rerun wasn't needed) and EpochFrom relinked.
**Confirmed on the real machine: `fits_header_update_test` now passes.**

Rebuilding/reinstalling wcslib a second time (needed here because `dis.c`
changed after the first successful install) surfaced one more
install-idempotency bug, logged as the sixth issue under
[MSYS2 + MinGW](#msys2--mingw) above (`cp -pR` failing to overwrite its
own previously-installed read-only headers) -- fixed the same way, with
`-f` added to the same two Makefile `cp` invocations.

This wcslib bug is worth reporting upstream to its author (Mark
Calabretta) -- it's a real regression that would affect any consumer on
any platform passing a forward-only SIP header (no `AP_*`/`BP_*` inverse
terms) to wcslib 8.9, not something specific to this project or to
Windows.

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

Follow [MSYS2 + MinGW](#msys2--mingw) start to finish -- it's the path
now confirmed to actually produce a working build, including wcslib
itself, which has no real route through MSVC/vcpkg (see
[MSVC + vcpkg](#msvc--vcpkg): both vcpkg and conda-forge confirmed to
have no Windows build for it, leaving only a from-scratch MSVC port of
its autotools build, real work nothing here has attempted -- worth
doing only if there's a concrete reason this project needs to stay on
MSVC rather than MinGW).

With the library build now confirmed working, what's next, in rough
order:

- Run the test suite (`ctest` from the build directory, or
  `cmake --build build-mingw --target test`) -- confirms behaviour, not
  just compilation, and hasn't been checked yet on this platform.
- Run the CLI and (if built) GUI executables directly and confirm they
  actually launch and do something sensible -- a clean build doesn't
  guarantee a `.dll` your executable needs is actually next to it or on
  `PATH` (Qt6's and wcslib's, in particular; `windeployqt` handles the
  Qt side on a normal Qt/MinGW setup, but hasn't been tried here).
  Running from the same MSYS2 MinGW64 shell that built it should put
  `/mingw64/bin` on `PATH` already; running the `.exe` from outside that
  shell (double-clicking it, or a plain `cmd.exe`) is the more realistic
  end-user scenario and is worth checking separately.
- ~~Try an actual end-to-end plate-solve~~ -- **done**, confirmed
  working via [ANSVR](#plate-solving-ansvr) after five real, separate
  fixes (see that section).
- ~~Try an actual end-to-end Gaia field query~~ -- **done**, confirmed
  working via a dedicated Python venv (see
  [Gaia field query: Python environment](#gaia-field-query-python-environment-confirmed-working-install-level)).
- Exercise the `.rc` icon resources (confirm the built `.exe`s actually
  show the intended icon in File Explorer/taskbar) -- still unverified.

## Release workflow

`.github/workflows/release.yml` builds EpochFrom for both Linux and
Windows and, on an actual version tag (`v*.*.*`), publishes both as a
GitHub Release via `softprops/action-gh-release`. It also runs on
`workflow_dispatch` (a manual "Run workflow" button, no tag needed) so
the pipeline itself can be exercised and debugged without cutting a
real release each time.

Three jobs:

- **`build-linux`** installs the exact package set from this README's
  own "Building" section (`qt6-base-dev libeigen3-dev wcslib-dev
  libcfitsio-dev cmake g++`), configures/builds/tests normally, then
  `cmake --install`s into a `usr/` tree and tars it up. This mirrors a
  completely ordinary, already-working Linux build -- nothing here is
  new or risky.
- **`build-windows`** reproduces the [MSYS2 + MinGW](#msys2--mingw)
  path this document describes end to end, non-interactively: it fetches
  the wcslib source tarball fresh, applies the same fixes confirmed
  necessary above (the TLS-specifier sed fix, the two `$(LN_S)`
  GNUmakefile sed fixes, `--disable-shared` to sidestep the SONAME-chain
  problem, and the wcsset()/MinGW asm-label patch via
  `scripts/ci/patch_wcslib_wcs_h.py`), builds and installs wcslib from
  source, then configures/builds/tests/installs EpochFrom itself against
  it, and finally runs `windeployqt` plus a manual `cfitsio` DLL copy to
  bundle a self-contained `dist/bin` tree before zipping it.
- **`release`** (tag pushes only) downloads both jobs' artifacts and
  publishes them on the GitHub Release for that tag, with
  auto-generated release notes.

**What's actually confirmed vs. new here:** every individual fix the
Windows job applies has been validated against real evidence -- the
`wcs.h` patch was diffed byte-for-byte against the real, working,
already-patched header on the machine this was all debugged on; the
`GNUmakefile` sed fixes match that machine's real final `GNUmakefile`
state; the TLS sed fix was checked to produce syntactically-valid
output. But the Windows job as a *whole pipeline*, run non-interactively
in actual GitHub Actions CI, has never been executed -- this is its
first draft. `windeployqt` in particular is flagged above (see "What to
actually do first") as never having been tried even interactively on
this project, so the "Bundle Qt/runtime/cfitsio DLLs" step is the first
real test of it, not a step that's already been confirmed solid. Treat
the first real run of this workflow (a `workflow_dispatch` run is the
cheap way to do that, without needing a tag) as another real diagnostic
opportunity, in keeping with everything else in this document -- not
something to assume just works because it was written carefully.

No extra repository secrets are needed: `softprops/action-gh-release`
uses the default `GITHUB_TOKEN`, and the `release` job already grants it
`contents: write`.
