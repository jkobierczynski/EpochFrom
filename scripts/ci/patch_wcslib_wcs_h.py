#!/usr/bin/env python3
"""
patch_wcslib_wcs_h.py
----------------------
Used only by .github/workflows/release.yml's Windows build, against a
freshly-fetched wcslib source tree, before configuring/building it.

wcs.h's own public, unconditional `int wcsset(struct wcsprm *wcs);`
collides with MinGW-w64's unrelated MSVC-compatibility `wcsset`
(wchar_t *wcsset(wchar_t*, wchar_t), declared in <string.h>/<wchar.h>)
the moment both are declared in the same translation unit -- essentially
guaranteed for any Qt-based consumer, since Qt's own headers pull in
<string.h> first. Confirmed on a real MSYS2/MinGW build of EpochFrom
itself (see docs/windows-port.md's MSYS2 + MinGW section for the full
story, including two earlier fix attempts -- a project-wide NO_OLDNAMES
define, then one scoped to just the files that include wcs.h -- that
were each confirmed wrong for the same reason: NO_OLDNAMES also gates
pid_t and other MinGW-w64 typedefs that Qt's own header chain needs).

The fix that actually works, applied here: gate wcslib's own `wcsset`
declaration behind a differently-named alias, bound to the real linker
symbol via GCC's asm-label extension, under MinGW only -- so consumers
(EpochFrom's own source included) keep calling plain wcsset() completely
unchanged, wcslib's declaration is otherwise untouched, and nothing needs
NO_OLDNAMES or any other project-wide define at all.

Idempotent: safe to run against an already-patched wcs.h (does nothing,
successfully) so a re-run of the workflow, or a locally cached wcslib
checkout, doesn't fail on a second pass.

Confirmed on a real run of this workflow: the freshly-extracted wcs.h can
come out with the Windows read-only attribute set (read_text() succeeds,
then write_text() fails with "PermissionError: [Errno 13] Permission
denied") -- wcslib's release tarball apparently ships this file without
the owner-write permission bit, which MSYS2's tar extraction carries over
as the Windows read-only attribute. Rather than pin down which upstream
tar entry/extraction step is exactly responsible, this script just makes
sure the file is owner-writable immediately before patching it.

Usage: patch_wcslib_wcs_h.py <path-to-wcslib's-C/wcs.h>
"""

import pathlib
import stat
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <path-to-wcs.h>", file=sys.stderr)
        return 2

    path = pathlib.Path(sys.argv[1])
    text = path.read_text()

    if "wcs_wcsset" in text:
        print(f"{path}: already patched, skipping")
        return 0

    old = "int wcsset(struct wcsprm *wcs);\n"
    new = (
        "#if defined(__MINGW32__) || defined(__MINGW64__)\n"
        "extern int wcs_wcsset(struct wcsprm *wcs) asm(\"wcsset\");\n"
        "#define wcsset wcs_wcsset\n"
        "#else\n"
        "int wcsset(struct wcsprm *wcs);\n"
        "#endif\n"
    )

    count = text.count(old)
    if count != 1:
        print(
            f"{path}: expected exactly one match for the wcsset declaration, "
            f"found {count} -- wcslib's own wcs.h may have changed; check this "
            "patch still applies before ignoring this failure",
            file=sys.stderr,
        )
        return 1

    # Clear the read-only attribute (if set) before writing -- see the
    # module docstring for why this is needed at all.
    path.chmod(path.stat().st_mode | stat.S_IWUSR)
    path.write_text(text.replace(old, new, 1))
    print(f"{path}: patched for MinGW wcsset collision")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
