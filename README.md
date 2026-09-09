<img src="docs/images/epochfrom-logo.svg" alt="EpochFrom logo: a star with a green arrow showing its direction of motion and a red line showing where it came from" width="96" height="96">

# EpochFrom

Determine the capture date of an astrophotography image by plate-solving it
and fitting Gaia DR3 stellar proper motions against the observed star
positions. Also profiles a given telescope/camera/corrector combination's
own optical distortion (a SIP-style polynomial, self-calibrated against
Gaia) so that distortion can be corrected rather than mistaken for
proper-motion signal.

Built Linux first, Qt/C++. Also builds and runs on Windows via MSYS2/MinGW
(confirmed on a real machine, CI-built) — see
[`docs/windows-port.md`](docs/windows-port.md) for the toolchain setup,
deploy steps, and what's still unverified (MSVC/vcpkg support is in the
CMake but untested; plate-solving needs a separately-installed local
solver such as ANSVR, since astrometry.net's own `solve-field` has no
native Windows build).

## Status

Early scaffold, but every pipeline stage is implemented in C++ and tested —
each is a direct port of a Python prototype validated interactively against
this project's own real capture library (2013–2018 North America/Pelican
Nebula sessions) before any of this code was written; see `docs/` for what
that prototyping found.

- **Epoch fitting** — Gaia catalog loading, rigorous proper-motion
  space-motion propagation, and a Levenberg-Marquardt fit. Regression-tested
  against real field data (`tests/epoch_fit_selftest.cpp`).
- **Plate-solving** — `PlateSolver` shells out to astrometry.net's
  `solve-field` and reads back center RA/Dec, field size, and pixel scale
  via wcslib (full TAN/TAN-SIP, distortion terms included). Tested
  (`tests/plate_solver_wcs_test.cpp`) and cross-checked against real solved
  fields. See `EpochFrom solve --help`.
- **Star detection & equipment profiling** — `StarDetector` finds point
  sources (tiled background estimate, Gaussian matched filter, DAOFIND-style
  sharpness cut against nebula texture/cosmic rays); `EquipmentCalibrator`
  runs the full single-epoch SIP calibration pipeline from
  `docs/equipment-profiling-spec.md` (Gaia cross-match, polynomial distortion
  fit with held-out cross-validation, internal repeatability diagnostic),
  saving the result as an `EquipmentProfile` JSON. Tested
  (`tests/star_detector_test.cpp`, `tests/equipment_calibrator_test.cpp`) and
  validated against a real 8-sub session. See `EpochFrom calibrate --help`.
  Doesn't claim full spec parity — see `EquipmentProfile::limitingFactor`'s
  doc comment for the one diagnosed case vs. the spec's manual-investigation
  categories.
- **End-to-end image dating** — `ImageDater` chains star detection, WCS (with
  optional equipment-profile correction), and the epoch fit into one
  estimate. Tested end to end (`tests/image_dater_test.cpp`). See
  `EpochFrom date --help`.
- **Desktop GUI** — `EpochFrom-gui`, a Qt Widgets app with Gaia/Solve/
  Calibrate/Date/Starfield tabs plus a **Project bar** (base directory +
  filter, with per-tab "Fill from Project"/"Fill All Tabs" buttons and
  editable path patterns) driving the same core library the CLI uses
  directly. Each tab runs its work on a background thread and shares the
  CLI's own report formatting. The Solve tab auto-seeds a directory batch's
  pointing/scale hints from its first single-image solve. The Starfield tab
  visualizes proper motion in a solved frame (circled fastest-movers with
  direction/origin vectors, fullscreen mode) and also ships as its own
  standalone app, `EpochFrom-starfield`, sharing settings with the main GUI.
  Not full CLI parity yet: `calibrate`'s `--sub`/`--wcs` pair-list mode is
  still CLI-only, and there's no in-app residual-field plot (`tools/
  residual-field.html` opens in a browser instead). See "Building" below.

Not yet ported to C++: automatic equipment-tagging for a dating run
(`--profile` stays an explicit, manual, by-default-required choice, opted
out of with `--noprofile`).

## Screenshots

A full run through the GUI (the recommended tab order — Gaia, then Solve,
then Calibrate, then Date — is printed in its status bar; the shots below
lead with Solve) against a real 39-sub Sadr/Ha session, plus the Starfield
viewer on its own.

**Solve tab** — batch plate-solving the session's 39 subs against a pointing
hint, one `.wcs` sidecar per image:

![Solve tab, batch plate-solving a directory of subs with a pointing hint](docs/images/EpochFrom-gui-solve.jpg)

**Gaia tab** — downloading a field's reference catalog, here centered from
an already-solved `.wcs` sidecar (0.9° radius, G<16, RUWE<1.4): 5154 stars
back from the archive, with the fastest-moving few printed for a sanity
check before they're saved to `gaia.csv`:

![Gaia tab, querying Gaia DR3 from a .wcs sidecar and listing the fastest-moving stars in the field](docs/images/EpochFrom-gui-gaia.jpg)

**Calibrate tab** — fitting an equipment distortion profile against those
39 subs' Gaia matches: order-4 polynomial, RMS falling from 1734.3 mas to
472.4 ± 2.8 mas held-out, with the fitted profile and per-sub RMS printed
below:

![Calibrate tab, fitting an equipment profile against Gaia](docs/images/EpochFrom-gui-calibrate.jpg)

`calibrate` on the CLI, same pipeline, run against a different session
(green channel) — the per-sub affine fit, per-sub residuals, and the
internal-repeatability diagnostic that flags when Gaia match quality and
sub-to-sub agreement disagree:

![CLI output of `EpochFrom calibrate`](docs/images/EpochFrom-calibrate.jpg)

**`tools/residual-field.html`** — that same Calibrate run's residuals
loaded into the spatial vector-field viewer, before correction (raw
linear-WCS residuals, 1729.4 mas RMS combined, axis ratio 1.36× — the
rotational swirl across the sensor is the signature of uncorrected optical
distortion):

![Residual field before the distortion correction, showing a rotational swirl pattern](docs/images/EpochFrom-residualbefore.jpg)

...and after the chosen-order polynomial fit (451.7 mas RMS, axis ratio
1.01× — the swirl is gone), with "Highlight top N" turned on to ring the
50 largest remaining outlier vectors in green, the stars still worth a
second look even after correction:

![Residual field after the distortion correction, with the top 50 outlier vectors highlighted in green](docs/images/EpochFrom-residualafter.jpg)

**Date tab** — batch-dating that same directory of subs against Gaia using
the equipment profile fitted above, ending in the inverse-variance
weighted average across all 39 images:

![Date tab, batch-dating a directory against Gaia with an equipment profile, showing the weighted average date](docs/images/EpochFrom-gui-date.jpg)

**Starfield tab** — the top 15 fastest-moving Gaia stars in one of those
subs, circled and arrowed at the sub's own capture epoch:

![Starfield tab, circling and vector-arrowing the fastest-moving Gaia stars in one sub](docs/images/EpochFrom-gui-starfield.jpg)

...and the same view in fullscreen (`EpochFrom-starfield`, or the tab's own
Fullscreen button) against a wider field, the proper-motion glyph the app's
logo is drawn from repeated once per star:

![Starfield viewer in fullscreen, showing proper-motion vectors for many stars across a wide field](docs/images/EpochFrom-starfield-hero.jpg)

### On Windows

The same GUI, same tab order, on a real MSYS2/MinGW build (see
[`docs/windows-port.md`](docs/windows-port.md)) — a 47-sub M51 LRGB session.

**Solve tab** — the directory already solved on a previous run, so this
pass just confirms all 47 `.wcs` sidecars are in place ("already solved,
skipping" is expected once a batch has been solved; re-solving is opt-in
via "Re-solve files that already have a .wcs sidecar"):

![Solve tab on Windows, confirming 47 already-solved subs in a directory batch](docs/images/EpochFrom-gui-solve-windows.jpg)

**Gaia tab** — querying from that batch's `.wcs` sidecar (0.9° radius,
G<16, RUWE<1.4): 636 stars back from the archive, saved to `gaia.csv`,
via a `uv`-managed virtual environment's `python.exe`:

![Gaia tab on Windows, querying Gaia DR3 and saving 636 stars to gaia.csv](docs/images/EpochFrom-gui-gaia-windows.jpg)

**Calibrate tab** — fitting an equipment profile against all 47 subs:
order-3 polynomial, RMS falling from 1630.1 mas to 268.6 ± 3.9 mas
held-out, with the worst-fitting subs and saved profile/residuals printed
below:

![Calibrate tab on Windows, fitting an equipment profile against 47 subs](docs/images/EpochFrom-gui-calibrate-windows.jpg)

**Date tab** — batch-dating the same 47 subs against Gaia with that
profile, ending in a weighted average of 2018-06-22 (epoch 2018.4746 ±
0.4939 yr):

![Date tab on Windows, batch-dating 47 subs to a weighted average date](docs/images/EpochFrom-gui-date-windows.jpg)

**Starfield tab** — the top 15 fastest-moving Gaia stars around M51 in one
of those subs:

![Starfield tab on Windows, circling the fastest-moving Gaia stars around M51](docs/images/EpochFrom-gui-starfield-windows.jpg)

...and the same view in fullscreen:

![Starfield viewer on Windows in fullscreen, showing proper-motion vectors around M51](docs/images/EpochFrom-starfield-hero-windows.jpg)

## Building

Dependencies (Ubuntu/Debian package names): `qt6-base-dev`, `libeigen3-dev`,
`wcslib-dev`, `libcfitsio-dev`, `cmake`, a C++17 compiler. Plate-solving
itself additionally needs astrometry.net's `solve-field` on `PATH` (with
index files matching your field size) — that's a separate install, not a
build dependency, and only needed at runtime for `EpochFrom solve` without
`--wcs-only`. The GUI additionally needs Qt6's widgets module -- on Debian/
Ubuntu that's pulled in by `qt6-base-dev` already; if it isn't (a minimal
Qt install can split it out), CMake configure prints a note and skips
`EpochFrom-gui` rather than failing the whole build -- pass
`-DEPOCHFROM_BUILD_GUI=OFF` to skip it deliberately and silence the note.

```
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

This builds `build/src/cli/EpochFrom` (the command-line tool used throughout
the rest of this README), `build/src/gui/EpochFrom-gui` (the desktop app),
and `build/src/gui/EpochFrom-starfield` (the standalone proper-motion
viewer, see the Starfield tab section above) -- just run any of them, no
arguments needed.

### Desktop integration (taskbar/dock icon)

Running the binaries straight out of `build/` this way, the GUI's window
does carry the app's own icon (title bar, alt-tab) from the moment it
opens -- that's set in-process via `QApplication::setWindowIcon()`, no
install required. What that *doesn't* get you, on most Linux desktops, is
an icon in the taskbar or dock: GNOME Shell/Ubuntu Dock in particular
resolve that one by matching the window to an installed `.desktop` entry,
not by reading the window's own icon, so an unpackaged build shows a
generic icon there even though nothing's actually wrong. To fix that too,
install it properly instead of just building:

```
cmake -B build -S . -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
```

That puts `EpochFrom-gui`/`EpochFrom-starfield` on `$HOME/.local/bin` (make
sure it's on `PATH`), installs `packaging/linux/*.desktop`, and installs
the app icon into the `hicolor` icon theme those `.desktop` files reference
(`$HOME/.local/share/icons/hicolor/<size>/apps/epochfrom.png`, plus an SVG
for `scalable`). Log out/in, or run `update-desktop-database
~/.local/share/applications` and `gtk-update-icon-cache` if your desktop
picks up new entries without that, to see it show up. This is a
Linux/freedesktop.org-specific mechanism -- it's not attempted on Windows
or macOS, which each have their own separate way of associating an icon
with a built executable.

`EpochFrom selftest --gaia tests/data/gaia_northamerica.csv` runs the same
synthetic epoch-recovery check as the regression test, but as a one-off you
can point at any Gaia catalog CSV and tweak the noise/epoch/star-count
knobs — handy for sanity-checking a new field before trusting a real fit
against it.

`EpochFrom solve <image>` plate-solves an image and prints its center
RA/Dec, field size, and pixel scale. Pass `--ra`/`--dec`/`--radius` to hint
the search (much faster than a blind solve), `--scale-low`/`--scale-high`
if you know your rig's approximate arcsec/pixel, or `--wcs-only` to skip
solving entirely and just read an already-solved `.wcs` file.

By default the solution is written only to the `.wcs` sidecar next to the
image (which is all the rest of this pipeline needs), leaving the original
light frame untouched -- and nothing else. `solve-field` itself defaults
to also writing several byproducts next to the image on a successful
solve (`<name>.new`, a full copy of the image with the WCS baked into its
header -- easy to mistake for a second capture -- plus `<name>.rdls`,
`<name>.match` and `<name>.corr`) and, on every attempt whether it solves
or not, `<name>.axy` (its intermediate source list); EpochFrom explicitly
suppresses all of these, since it already gets everything it needs from
the `.wcs` sidecar. Pass `--update-fits-header` to *also* copy the WCS
(CRVAL/CRPIX/CD, any SIP distortion terms, and convenience decimal `RA`/
`DEC` keys) directly into the image's own FITS header, in place -- useful
if you want your light frames self-describing for other tools that don't
know to look for a sidecar. This modifies the original file, so it's off
by default; it deliberately never touches `OBJCTRA`/`OBJCTDEC` (the mount's
own record of where it was asked to point, left alone on purpose). The same
flag is accepted by `calibrate` and `date`, applying to any sub they
auto-solve along the way.

`EpochFrom solve --dir <dir>` batch-solves every `.fits`/`.fit`/`.fts` file
in a directory in one go, instead of invoking `solve` on each file by hand.
Files that already have a matching `<name>.wcs` sidecar next to them are
skipped by default (pass `--force` to re-solve them anyway); the same
pointing/scale hints as single-file `solve` apply to every file in the
batch. Prints a per-file line plus a final solved/skipped/failed tally.

`EpochFrom calibrate --dir <dir> --gaia <catalog.csv> --outprofile profile.json`
fits an equipment distortion profile from a directory of `<name>.fits` +
`<name>.wcs` pairs. `--outprofile` should be given a `.json` filename — it's
the file `date --profile` reads back in; `calibrate` will still write it if
you don't, but prints a note reminding you. Any `.fits` file in the
directory that doesn't yet have a matching `.wcs` is solved automatically
before calibrating (same `--ra`/`--dec`/`--radius`/`--scale-low`/
`--scale-high`/etc. hint options as `solve` are accepted here too, and
apply to that auto-solve step) — so pointing `--dir` at a folder of raw
subs is enough; you don't need to run `EpochFrom solve` on each one
yourself first, though you still can if you'd rather solve and inspect
them individually (see `docs/equipment-profiling-spec.md` section 3 for
why per-sub solving is what the fit is built on). `--sub`/`--wcs` pairs
work too if your subs and their `.wcs` files aren't conveniently
co-located. Prints the before/after RMS, the cross-validated
order-selection table, the internal-repeatability diagnostic, and a
per-sub residual report (each sub's observation count and before/after
RMS, or why it was skipped -- no stars detected, no cross-matches,
unreadable image/WCS, etc. -- plus the worst-fitting subs called out
separately), then saves the fitted profile as JSON. That report is what
tells you whether a large aggregate residual is a genuine distortion
spread evenly across the session or a handful of bad subs (a meridian
flip, a guiding hiccup, clouds) dragging the pooled numbers up.
`--corrector-type refractive` also prints the narrowband-filter guidance
from the spec's controlled comparison.

Each sub is plate-solved independently, so its own small rotation/scale
error is unique to that frame and would otherwise scramble a single
distortion polynomial shared across every pooled sub. By default,
`calibrate` first fits and removes each sub's own translation/rotation/
scale/shear (against a fixed chip-center reference pixel, not that sub's
own solved CRPIX) before the shared, higher-order distortion fit runs —
pass `--no-per-sub-affine` to disable this and go back to a single pooled
fit. The report includes a per-sub rotation/scale/shear table and flags
when subs disagree on position angle by more than 0.02°, which is the
signature of per-sub plate-solve inconsistency rather than optics.

Pass `--residuals-csv <path>` to also dump every pooled star observation
(pixel position relative to the fixed reference, radius, before/after
ξ/η residual, sub index, kept-in-fit flag) to a CSV -- give it a `.csv`
filename (again, just a note if you don't). Open
`tools/residual-field.html` in a browser and load that CSV to inspect it
visually: a vector field of the residuals across the sensor, radius and
per-sub scatter plots, and histograms, all rendered client-side (nothing
is uploaded). It's the fastest way to tell a radially-symmetric cause
(field curvature/scale) apart from a tangential/swirl one (rotation —
per-sub plate-solve position-angle error) or one tied to a single sub
(guiding, meridian flip, clouds). The vector field panel's "Highlight top
N" control (1-50) rings the N stars with the single largest residual --
the outshoot -- in green with an arrow, on top of the ordinary vector
cloud, so the very worst offenders (a bad cross-match, a genuinely
distorted corner) are easy to pick out from a session's worth of points at
a glance instead of hunting through tooltips one by one; it's ranked over
every point in the CSV, not just the ones the panel happens to plot for
legibility, so a real outlier is never missed just because sampling left
it out of the drawn cloud.

`EpochFrom date <image> --gaia <catalog.csv> --profile <profile.json>`
estimates a single image's capture date: detects stars, converts them to
sky coordinates, and fits the epoch at which Gaia's proper-motion-
propagated positions best match what was observed. If `<image>` doesn't
have a matching `.wcs` sidecar yet, it's solved automatically first (same
solve-hint options as `solve`/`calibrate` apply here too). `--profile`
(from `calibrate`) is required by default -- it corrects each detected
star's position with that rig's calibrated distortion model instead of
trusting the platesolver's own SIP fit, which is usually the difference
between a date good to within a day or two and one whose uncertainty is
measured in years, since an uncorrected rig's positional error can swamp
the multi-year proper-motion signal the fit depends on (see
`docs/equipment-profiling-spec.md` section 1). Pass `--noprofile` to
explicitly opt out and date against the platesolver's own uncorrected WCS
instead -- `date` refuses to run with neither flag, rather than silently
producing a date that can be off by years. Prints the estimated date, the
fitted epoch and its uncertainty, the RMS residual, and warns if the
fitted date falls outside the profile's `valid_from`/`valid_to` range (the
equipment may have been adjusted since calibration). `EpochFrom date --dir
<dir> --gaia <catalog.csv> --profile <profile.json>` batch-dates every
`.fits`/`.fit`/`.fts` file in a directory the same way (auto-solving
missing `.wcs` files as it goes), printing one line per file plus a
dated/failed tally, followed by a **weighted average date**: every
successfully-dated image's own epoch estimate combined into one
inverse-variance-weighted average (weight = `1 / epochSigmaYears^2`,
`ReportFormatting::combineDateEstimates`), so a sub whose fit landed a
tighter epoch uncertainty (more/better-matched stars, lower residual RMS)
pulls the combined date toward itself more than a noisier one, rather than
every sub in the directory counting equally regardless of how good its own
fit was. An image whose fit didn't converge, was rank-deficient (its
reported uncertainty is documented as optimistic in that case, and letting
it into the weighting could let a falsely tiny sigma dominate the average
purely from an unreliable number, not real precision), or somehow has no
finite/positive sigma is left out of the weighted average and counted
separately, even though it's still counted as "dated" in the tally above
it. The GUI's Date tab does the same for its own directory/batch runs,
showing the combined result in its summary line above the per-file log.

## Gaia data

`scripts/gaia_field_query.py` queries the Gaia DR3 archive for a field's
stars (position, proper motion, parallax, RUWE-filtered) and writes a CSV
in the format `GaiaCatalog::loadCsv` reads. Run it wherever you have
unrestricted network access to the Gaia archive — it's a separate step from
the C++ tool on purpose, not a build dependency (it needs
`pip install astropy astroquery`, which the GUI/CLI build doesn't). Run it
from a terminal, or from `EpochFrom-gui`'s Gaia tab, which shells out to it
via `QProcess` and streams its output live.

The field center it queries around can come from `--fits <image>` (reads
`CRVAL1/2`, falling back to `RA`/`DEC`, falling back to `OBJCTRA`/
`OBJCTDEC`), `--wcs <sidecar.wcs>` (an already-solved `.wcs` file -- the
most reliable source when you have one, and it doesn't require re-parsing
the often much larger light frame), explicit `--ra`/`--dec`, or a
known-good `--target` preset. The Gaia tab exposes all four as radio
buttons.

It identifies itself to the archive as `EpochFrom/0.1.0` (overriding
astroquery's own `astroquery/<astroquery version> ...` default) so Gaia
sees which tool is actually making the requests; keep the version string
here in sync with the C++ project's own version if that ever changes.

## Docs

- `docs/equipment-profiling-spec.md` — design notes for the equipment
  self-calibration feature (SIP distortion fit against Gaia), including the
  cross-validation and filter-choice findings that need to carry through to
  the implementation.
- `docs/test-run-pelican-ic5070.txt` — a real end-to-end run (build,
  `selftest`, `calibrate`, `date --profile`) against a 30-sub Pelican
  Nebula (IC 5070) Ha session with a known capture date (2017-08-28,
  processed image
  https://www.flickr.com/photos/jurgenk2/36828617626/in/album-72157646734292486),
  for comparing future runs against. Per-file date estimates cluster within
  ~17 days of the true date on average (mean offset 17 d; per-frame
  uncertainty ~3 yr).
