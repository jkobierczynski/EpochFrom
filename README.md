# EpochFrom

Determine the capture date of an astrophotography image by plate-solving it
and fitting Gaia DR3 stellar proper motions against the observed star
positions. Also profiles a given telescope/camera/corrector combination's
own optical distortion (a SIP-style polynomial, self-calibrated against
Gaia) so that distortion can be corrected rather than mistaken for
proper-motion signal.

Built Linux first, Qt/C++, with a Windows port planned afterward.

## Status

Early scaffold. The core epoch-fitting engine (Gaia catalog loading, rigorous
proper-motion space-motion propagation, and the Levenberg-Marquardt epoch
fit) is implemented and regression-tested against real field data — see
`tests/epoch_fit_selftest.cpp`. It's a direct C++ port of a Python
prototype that was validated interactively against this project's own real
capture library (2013–2018 North America / Pelican Nebula sessions) before
any of this code was written; see `docs/` for what that prototyping found.

Plate-solving is also ported: `PlateSolver` shells out to `astrometry.net`'s
`solve-field` (same as the prototype) and, either from that or directly from
an existing `.wcs` sidecar, reads back center RA/Dec, field width/height,
and pixel scale via wcslib (honoring the full TAN/TAN-SIP projection, SIP
distortion terms included, rather than a naive linear shortcut). Covered by
a deterministic synthetic-fixture unit test (`tests/plate_solver_wcs_test.cpp`)
and cross-checked manually against real solved fields from the Python
prototyping. See `EpochFrom solve --help`.

Star detection and equipment profiling are also ported. `StarDetector` finds
point sources in a light frame (tiled/interpolated local background
estimate, a Gaussian matched filter sized to the expected FWHM, and a
DAOFIND-style sharpness statistic to reject nebula texture and cosmic rays
— a real problem on this project's own bright-nebula test frames, not a
theoretical one; see the class comment in `StarDetector.h` for what that
fixed). `EquipmentCalibrator` runs the full single-epoch SIP calibration
pipeline from `docs/equipment-profiling-spec.md`: cross-match detections
against Gaia (propagated to each sub's own `DATE-OBS`), pool across subs,
fit a 2D distortion polynomial at each candidate order with held-out
cross-validation, pick the order at which held-out RMS stabilizes, and
compute the internal (sub-to-sub, no-Gaia) repeatability diagnostic that
separates "centroiding is the limit" from "there's a real remaining
distortion." `EquipmentProfile` saves the fitted model as JSON. Covered by
a synthetic-star unit test (`tests/star_detector_test.cpp`) and an
end-to-end synthetic-distortion integration test
(`tests/equipment_calibrator_test.cpp`), plus manual validation against a
real 8-sub 2018 North America Nebula session from the Python prototyping —
order selection and residual levels land in the same ballpark the
prototype's own cross-validated results did. See `EpochFrom calibrate
--help`.

This implementation deliberately doesn't claim everything the spec
discusses: `StarDetector` is the same family of technique as
DAOStarFinder/DAOPHOT, not a bit-exact port of it, and
`EquipmentProfile::limitingFactor` only auto-diagnoses the one comparison
the spec calls required (internal repeatability vs. held-out residual) —
it reports `measurement_precision` or `unclear`, not the spec's illustrative
`catalog_depth`/`detection_noise` categories, which the spec itself
describes as manual investigations rather than something one calibration
run's numbers can tell apart on their own.

Dating a real image end to end is now wired up too: `ImageDater` ties
`StarDetector`, a WCS reader, and `EpochFit` together into one estimate —
detect stars in a light frame, convert each to sky coordinates (via the
platesolver's own WCS, SIP terms included if it fit any, or via a saved
`EquipmentProfile`'s calibrated correction instead when one is given, per
`docs/equipment-profiling-spec.md` section 9), and fit the capture epoch
against Gaia. Covered by an end-to-end synthetic-distortion integration
test (`tests/image_dater_test.cpp`) that checks both the no-profile and
with-profile paths recover a known injected epoch, plus that applying the
profile measurably reduces the residual over not applying it to the same
distorted data. See `EpochFrom date --help`.

A first desktop GUI is here too: `EpochFrom-gui` is a Qt Widgets app with a
Gaia/Solve/Calibrate/Date tab apiece, driving the same core library
(`epoch_from_core`) the CLI does directly rather than shelling out to it
(the Gaia tab is the one exception -- see below). Each tab exposes that
command's options as fields instead of flags, runs the actual work (a
solve, a calibration directory batch, a dating run) on a background thread
so the window stays responsive during a long solve, and prints its report
using the exact same formatting code the CLI uses
(`src/core/ReportFormatting.{h,cpp}`, factored out for this reason) so the
two can't quietly drift apart on what a result says. It's a first pass, not
full parity with the CLI: `calibrate`'s `--sub`/`--wcs` pair-list mode (for
subs that aren't conveniently co-located in one directory) is still
CLI-only, and there's no in-app residual-field plot yet -- the Tools menu
just opens `tools/residual-field.html` in a browser, same as the CLI's own
suggestion after a `--residuals-csv` export. See "Building" below for how to
build it (or skip it) and where the binary ends up.

The Gaia tab wraps `scripts/gaia_field_query.py` (see "Gaia data" below) as
a subprocess via `QProcess`, streaming its progress into the same kind of
log view the other tabs use, so downloading a field's reference catalog no
longer requires a terminal.

A **Project** bar sits above the tabs holding a base directory and a
filter (Ha/OIII/SII/L/R/G/B/... or blank), persisted between runs. Every
tab has a "Fill from Project" button that composes that tab's paths from
it: subs directory is `<base>/<filter>` (or `<base>` itself with no
filter), Gaia catalog is `<base>/gaia.csv` and equipment profile is
`<base>/profile.json` (both shared across filters, since neither the star
field nor the rig's own distortion depends on which filter a sub was shot
through), and a calibration's residuals CSV is
`<base>/<filter>_residuals.csv`. It's a one-click convenience, not a
constraint -- every field it fills stays a plain, freely-editable path
afterward, and nothing requires using it at all.

Each tab's options sit in a scroll area above its command-output log, with
the divider between them a `QSplitter` -- drag it, or click one of the
"Balanced"/"More Output"/"More Options" presets in the action bar, which
also keeps the tab's primary button (Solve/Query Gaia/Calibrate/Date)
visible regardless of scroll position. Every spin box and combo box in the
GUI also ignores mouse-wheel scrolling unless it currently has keyboard
focus (`src/gui/NoWheelWidgets.h`), since Qt's default behavior -- accepting
wheel input on mere hover -- meant scrolling down a tab's options could
silently change whatever numeric field the cursor happened to be over
instead of scrolling the page.

Not yet ported to C++: any automatic equipment-tagging for a dating run
(today `--profile` is an explicit, manual, and by-default-required choice
per `date` invocation, opted out of with `--noprofile` — the spec's
suggestion of auto-detecting equipment from frame metadata is flagged there
as needing a manual override path anyway, since this library's own header
metadata was found stale in places during prototyping).

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

This builds both `build/src/cli/EpochFrom` (the command-line tool used
throughout the rest of this README) and `build/src/gui/EpochFrom-gui` (the
desktop app -- just run it, no arguments needed).

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
(guiding, meridian flip, clouds).

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
dated/failed tally.

## Gaia data

`scripts/gaia_field_query.py` queries the Gaia DR3 archive for a field's
stars (position, proper motion, parallax, RUWE-filtered) and writes a CSV
in the format `GaiaCatalog::loadCsv` reads. Run it wherever you have
unrestricted network access to the Gaia archive — it's a separate step from
the C++ tool on purpose, not a build dependency (it needs
`pip install astropy astroquery`, which the GUI/CLI build doesn't). Run it
from a terminal, or from `EpochFrom-gui`'s Gaia tab, which shells out to it
via `QProcess` and streams its output live.

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
