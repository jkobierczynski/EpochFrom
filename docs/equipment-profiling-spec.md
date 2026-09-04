# Equipment Profiling — Design Notes

Status: implemented in the C++ tool (`StarDetector`, `EquipmentCalibrator`, `EquipmentProfile`, the `EpochFrom calibrate` CLI command). Originally written as a draft ahead of that implementation, derived from prototype scripts and empirical tests run against real capture data (2016–2018 North America / Pelican Nebula sessions, ASI1600MM-Cool + 800mm F/4 reflector + Paracorr); left as-is below as the design record. See the bottom of this file for where the shipped implementation deviates.

## 1. Problem this feature solves

Every rig has its own optical distortion signature: field curvature, coma, and — for any setup using a refractive corrector (Barlow, reducer, coma corrector) — chromatic aberration. A plain platesolve gives a linear (TAN) WCS, or at best a low-order SIP tweak fit internally by the solver against its own sparse index catalog (Tycho-2, in astrometry.net's case). That's not enough: prototyping against this rig's real 800mm F/4 reflector + Paracorr showed a linear-only solve leaves ~1.6" RMS positional error against Gaia, which swamps the multi-year proper-motion signal the dating pipeline depends on (typically 10–20mas for a "boring" star, more for a genuine high-PM one).

Equipment Profiling lets a user calibrate their own rig once, using a single imaging session and Gaia DR3 as an absolute reference, producing a reusable distortion model that every future dating run for that equipment can apply.

## 2. Core idea: self-calibrate from one epoch against Gaia, not two epochs against each other

Two approaches were prototyped:

- **Differential astrometry** (compare the user's own frames from two epochs directly, hoping shared distortion cancels). This only works if both epochs used *identical* optics — mismatched equipment reintroduces the same distortion problem it's meant to avoid, and even with matched equipment (same reflector, 2.1yr baseline, 240 shared stars), the achievable residual floor was ~450–500mas — nowhere near clean enough, and mixes in each epoch's independent centroiding noise.
- **Single-epoch SIP fit against Gaia** (this feature). Detect stars in one calibration session, cross-match against Gaia DR3 (propagated to the frame's own observation epoch via `apply_space_motion`, not left at Gaia's 2016.0 reference epoch), and fit a 2D polynomial distortion model directly against that absolute reference. This is strictly better: it doesn't need a second epoch at all, and it isolates *this rig's* distortion rather than two rigs' combined error.

This is the approach Equipment Profiling implements.

## 3. Calibration workflow

1. **User selects a calibration session** — one imaging session from one piece of equipment (telescope + corrector + camera + filter combination). Multiple subs from that session are used together (pooling subs materially stabilizes the fit — see §5).
2. **Platesolve each sub** (existing pipeline step), individually — not by reusing one sub's WCS across the session, since even light dithering breaks that assumption. Strip the solver's own built-in SIP tweak back to a pure linear (CRVAL/CRPIX/CD) WCS; that linear solution is the "before" baseline the polynomial model improves on.
3. **Detect stars** in each sub (DAOStarFinder or equivalent), keep pixel centroid + flux.
4. **Query or reuse a Gaia DR3 field catalog** covering the session's pointing. Depth matters less than expected past a few hundred usable stars (§5) — G<14–15 is normally sufficient; deeper isn't harmful but showed no measurable benefit in testing once detection was already the limiting factor, not catalog size.
5. **Propagate Gaia positions to the calibration frame's own DATE-OBS** via space motion (parallax + proper motion + radial velocity where available). Skipping this and comparing against Gaia's raw 2016.0 positions introduces an error up to ~(baseline years × proper motion) for each star — small per star, but a needless systematic to avoid for free.
6. **Cross-match** detected stars to the epoch-propagated Gaia catalog (nearest neighbor, 1:1 deduplication, few-arcsec tolerance).
7. **Fit a 2D polynomial distortion model** in pixel offset from CRPIX, predicting the tangent-plane residual (Gaia true position − linear-WCS-predicted position). This is fit per output axis (ξ, η) as a polynomial in normalized pixel coordinates (see §4 for why normalization matters and how order is chosen).
8. **Report before/after residuals to the user** — RMS and median, in mas, alongside a plain-language read on whether the improvement is trustworthy (§5's cross-validation, not just in-sample fit quality).
9. **Save the fitted model as an Equipment Profile** (§6), which future dating runs for frames tagged with that equipment can load and apply automatically.

## 4. Fitting details that matter (learned the hard way)

- **Normalize pixel coordinates** before building the polynomial design matrix (divide by roughly half the sensor's long axis). Un-normalized raw-pixel powers above order ~3 are severely ill-conditioned in a least-squares solve on a 4000+ px sensor — order-5 fits without normalization blew up (RMS got *worse* going from order 4 to 5); after normalizing, order 5–6 fits were stable and slightly better than order 3–4.
- **Sigma-clip iteratively** while fitting (3–3.5σ, a handful of iterations) to keep a handful of bad cross-matches (blends, misidentifications) from dragging the polynomial around.
- **Fit against tangent-plane residuals in mas**, differencing two comparable-magnitude angles before converting to mas — never perturb a ~300° absolute RA by a mas-scale offset directly, that's silently lost to float64 precision.

## 5. Order selection MUST be cross-validated, not chosen by in-sample fit quality

This was the single biggest and most surprising finding of the prototyping work, and it needs to be a first-class part of the implementation, not an afterthought.

Fitting on all available matched stars and looking at in-sample RMS is actively misleading: a single-sub test with 129 matched stars showed an apparent 16x improvement (1618mas → 98mas) at order 4 — which fell apart under cross-validation (fit on half the stars, evaluate on the held-out half) to a real ~608mas, because 129 stars can't safely constrain a 30-parameter model. Pooling 8 subs of the same session (1049 star-observations, ~130–290 distinct stars) stabilized this considerably: held-out RMS plateaued at ~450–510mas from order 3 through 6, which is the trustworthy number.

**Required behavior**: the tool must fit each candidate order (1 through ~6) with a held-out cross-validation split (e.g. 20 random 50/50 splits, report mean ± std of held-out RMS), and:
- Pick the lowest order at which held-out RMS stabilizes (stops improving materially with higher order) — not the order with the single lowest in-sample number.
- Warn explicitly if held-out RMS is unstable or grows with more parameters (a clear overfitting signal) rather than silently picking a bad model.
- Show the user both numbers — in-sample and held-out — so an experienced user isn't misled by a headline improvement factor that won't hold up on new data.

## 6. Diagnosing what's actually limiting precision

More reference stars is not always the answer, and the tool should say so rather than just prompting the user to get a deeper catalog. Prototyping against a 10x deeper Gaia catalog (G<13 → G<15.8, ~130 → ~5500 field stars) produced almost no improvement in held-out RMS (462–474mas vs 450–510mas) — because star *detection* in the image, not catalog depth, was already the bottleneck (nearly every detected star was already finding a Gaia match). Loosening the detection threshold to pull in more, fainter stars made things measurably *worse* (598–607mas), since fainter detections have noisier centroids that outweigh the benefit of more data points.

**Required diagnostic**: before or alongside the SIP fit, compute **internal repeatability** — the scatter of each star's measured position across the session's individual subs, with no Gaia comparison involved at all. This cleanly separates two very different problems:
- If internal repeatability is already close to the post-fit residual, the ceiling is **measurement/centroiding precision**, and more reference stars or higher polynomial order won't help — the fix is better data (see §7), not a better fit.
- If internal repeatability is much tighter than the post-fit residual, the ceiling is a genuine **unmodeled distortion** (or a fit/matching bug), and a different or higher-order model, or more stars, is the right lever.

Report both numbers to the user plainly: "your subs agree with each other to ~150mas, and match Gaia to ~500mas after calibration — the gap points to a real, if modest, remaining distortion" vs. "your subs only agree with each other to ~450mas — better data would help more than a fancier model."

## 7. Filter/corrector guidance (the controlled result)

A same-night, same-equipment, same-target test (2017-08-28 Pelican session, which happened to capture Hα, clear, red, green and blue back-to-back) gave a clean, controlled comparison:

| | detected/matched per sub | internal repeatability (median/RMS) | held-out SIP RMS (order 3–6) |
|---|---|---|---|
| Clear (broadband) | 453–499 / 288–325 | 276 / 476mas | ~980–1015mas |
| Hα (narrowband) | 277–356 / 205–258 | 147 / 301mas | ~530–565mas |

Narrowband was roughly **twice as precise** as clear on this rig, despite clear detecting more (fainter, continuum) stars. The likely mechanism: this rig's corrector (Televue Paracorr) is a refractive (glass) element, and glass has chromatic aberration; a clear/broadband exposure integrates that chromatic blur across the full visible bandpass, while a narrowband filter isolates a few nm and mostly avoids it. Differential atmospheric refraction (also chromatic, worse without an ADC) likely compounds the same effect.

**Required behavior**:
- Equipment Profiles should record whether the optical train includes a refractive corrector/reducer/Barlow element (vs. all-reflective, or a catadioptric system with a small/thin corrector plate).
- When a profile is flagged as having a refractive element, the calibration wizard should recommend the user's narrowest-bandpass available session for calibration, and surface a note if they pick a broadband (clear/L/RGB) session instead ("this equipment's corrector may add extra chromatic blur in broadband — a narrowband session, if you have one, will likely calibrate more precisely").
- This is a recommendation, not a hard block — an all-mirror system (no refractive corrector) has no strong prior reason to prefer narrowband, and this should be re-tested per-equipment-class rather than assumed universal.

## 8. Equipment Profile data model

A profile is the reusable output of one calibration run, keyed to a specific optical configuration (not just "a telescope" — backfocus/spacing changes, as seen between this rig's 2016 and 2018 sessions at ~0.1% plate-scale drift, mean a profile should be treated as valid for a date range, not forever).

Suggested fields:

```
EquipmentProfile
  id
  label                     (user-facing name, e.g. "800mm reflector + Paracorr, ASI1600MM")
  telescope_aperture_mm
  focal_length_mm           (nominal; actual fitted plate scale is stored separately)
  corrector_type            (none | refractive | reflective | catadioptric)
  camera_model
  pixel_size_um
  calibration_filter        (filter used for THIS fit, e.g. "Ha")
  valid_from / valid_to     (date range this profile is trusted for; null valid_to = still current)

  # fit inputs / provenance
  calibration_session_ref   (which subs were used)
  n_subs_used
  reference_catalog         (Gaia DR3, mag limit used)
  n_stars_matched
  detection_threshold_sigma

  # fit outputs
  crpix1, crpix2             (reference pixel the polynomial is defined about)
  pixel_scale_norm            (normalization constant used for the polynomial terms)
  poly_order_chosen
  poly_coeffs_xi[]             (A_pq terms)
  poly_coeffs_eta[]            (B_pq terms)

  # fit quality (both numbers, always shown together)
  rms_before_mas
  rms_after_insample_mas
  rms_after_heldout_mas
  internal_repeatability_median_mas
  internal_repeatability_rms_mas

  # guidance flags
  chromatic_corrector_warning_shown   (bool)
  limiting_factor                     (enum: catalog_depth | detection_noise | measurement_precision | unclear)
```

`limiting_factor` is set automatically from the §6 diagnostic (comparing internal repeatability to post-fit residual) and drives the user-facing guidance text.

## 9. Applying a profile

Once saved, a profile attaches to future dating runs: when a frame is tagged (or auto-detected, if enough header metadata is trustworthy — recall this library's own OBJCTRA/OBJCTDEC was found stale in places, so auto-detection needs a manual override path) as having used a given equipment configuration, the pipeline applies that profile's polynomial correction to the frame's linear WCS before doing the Gaia proper-motion fit, instead of relying on the platesolver's own low-order tweak alone.

If a frame's DATE-OBS falls outside a profile's `valid_from`/`valid_to`, warn the user that the equipment may have been adjusted since calibration (spacing, refocus, etc.) and offer to recalibrate.

## 10. Open items for the real implementation

- Test red/green/blue individually against the same 2017-08-28 controlled session to see whether precision degrades monotonically with bandpass width (would further support the chromatic-corrector theory) or something more specific to Hα is going on.
- Re-run the "does catalog depth matter" test on a richer star field (this session's ~300 detected stars/sub is thin; a lower-galactic-latitude field would stress-test whether the plateau in §6 is universal or specific to this sparse field).
- Decide the recalibration cadence / drift-monitoring story: the observed ~0.1% plate-scale drift between 2016 and 2018 on the same nominal setup suggests profiles should probably prompt for recalibration on a time or usage cadence, not just be "fit once, forever."
- Decide how `poly_order_chosen` interacts with very small calibration sessions (few subs, few matched stars) — should the wizard refuse to fit above order 2 without enough stars, rather than let a user unknowingly save an overfit profile?

## 11. Implementation notes (added post-implementation)

What shipped in `EquipmentCalibrator`/`StarDetector`/`EquipmentProfile` follows the design above closely, with a few concrete decisions worth recording:

- **Star detection is not a DAOStarFinder port.** It's the same family of technique (matched-filter convolution, local-maximum peak finding, sharpness-gated) but a from-scratch implementation, since photutils isn't available to link against from C++. One real bug surfaced and got fixed during implementation, not just a theoretical risk: a single frame-wide sigma-clipped background level is a poor model for this project's actual subject matter (bright, broad emission-nebula glow), and left uncorrected it produced tens of thousands of spurious "detections" per sub (nebula texture riding on the smooth glow trend, each a local maximum) against ~1000-1500 genuine Gaia-matched stars — both wrong and, since every candidate gets a centroiding pass, the reason a real 8-sub calibration run initially took over two minutes and had to be killed. Fixed with two changes: a coarse tiled + bilinearly-interpolated local background estimate (removes the smooth trend before thresholding, the same idea as photutils' `Background2D`) and a DAOFIND-style sharpness statistic (Stetson 1987) gating each candidate peak, which is what actually discriminates a point source from extended texture. After both fixes, a real North America Nebula Hα sub goes from ~44,000 spurious detections and 22 seconds to ~1,300-1,700 detections (matching order-of-magnitude with the Python prototype's own DAOStarFinder counts on comparable fields) and under 2 seconds.
- **§5's order-selection rule is implemented as: stop at the first order whose improvement over it is smaller than the next order's own held-out standard deviation** (i.e. the improvement is statistically indistinguishable from noise), with an explicit overfitting flag (and warning text) if a higher order's held-out RMS is actually worse by more than its own scatter. Validated on the real 2018-09-01 North America session (8 subs, `gaia_northamerica_deep.csv`): order selection correctly plateaus at order 3 (held-out RMS ~572-573mas from order 3 through 6), qualitatively matching this document's own §5 finding of a plateau from order 3 through 6 on the same session, though at a somewhat higher absolute mas floor than originally recorded here (this implementation's detector found roughly 2x more matched stars per sub than the original prototype run, among other differences — not expected to reproduce the Python prototype's numbers exactly, just its qualitative behavior).
- **§8's `minObservationsPerParameter` guard (the open item in §10 about small sessions) is implemented as a 5x-observations-per-total-parameter threshold** (`options.minObservationsPerParameter`, default 5.0): a candidate order is only fit and offered at all if the pooled observation count is at least `5 * 2 * (order+1)(order+2)/2`. A session too small for even order 1 is reported as an error rather than silently returning something.
- **§6's `limiting_factor` is intentionally narrower than the illustrative enum in §8.** This implementation only auto-diagnoses the one comparison the spec calls a required diagnostic — internal (sub-to-sub) repeatability vs. the chosen order's held-out residual — and reports `"measurement_precision"` or `"unclear"`. It does not claim to automatically distinguish `catalog_depth` or `detection_noise`, which §6 itself describes as manual investigations (rerun with a deeper catalog; rerun with a different detection threshold) rather than something derivable from a single calibration run's own numbers.
- **§9 (applying a saved profile to a dating run) is now implemented**, as `ImageDater` and the `EpochFrom date` CLI command. `date` detects stars in the target image and converts each to sky coordinates one of two ways: with `--profile <profile.json>`, from the image's plain linear WCS (CRVAL/CRPIX/CD only) plus that profile's fitted polynomial correction, evaluated at the detected star's pixel offset from the *profile's* `crpix1`/`crpix2` (not the image's own solved CRPIX -- the polynomial is anchored to the physical sensor's pixel grid, which stays fixed across different pointings/solves of the same rig); without `--profile`, from the platesolver's own WCS directly (SIP terms honored if it fit any), which is the "platesolver's own low-order tweak" baseline this section describes a profile as improving on. `valid_from`/`valid_to` is checked against the *fitted* epoch (not a trusted DATE-OBS -- there usually isn't one, that's the point of dating in the first place) and surfaced as a warning, not a hard block, matching this section's "warn ... and offer to recalibrate" rather than refuse outright. Equipment tagging is still a fully manual `--profile` flag per invocation, not auto-detected from frame metadata -- deliberately, per this section's own caution about this library's OBJCTRA/OBJCTDEC having been found stale in places.
