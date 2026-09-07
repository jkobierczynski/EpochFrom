add_executable(epoch_fit_selftest epoch_fit_selftest.cpp)
target_link_libraries(epoch_fit_selftest PRIVATE epoch_from_core Qt6::Core)

# Regression test: recovers a known, injected epoch from real Gaia field
# data (not fabricated stars) to within a tight tolerance. This is the same
# check fit_epoch.py --selftest validated in the Python prototype this
# engine is ported from -- if this ever fails, something in the
# propagation/cross-match/fit chain broke, not just "the data is noisy".
add_test(NAME epoch_fit_selftest
         COMMAND epoch_fit_selftest "${CMAKE_CURRENT_SOURCE_DIR}/data/gaia_northamerica.csv")

add_executable(plate_solver_wcs_test plate_solver_wcs_test.cpp)
target_link_libraries(plate_solver_wcs_test PRIVATE epoch_from_core Qt6::Core)

# Unit test for the WCS-parsing math (readWcsFile), against a synthetic
# fixture built with known values rather than a real solve-field output --
# see the file for why. Deterministic and has no dependency on solve-field
# being installed.
add_test(NAME plate_solver_wcs_test COMMAND plate_solver_wcs_test)

add_executable(fits_header_update_test fits_header_update_test.cpp)
target_link_libraries(fits_header_update_test PRIVATE epoch_from_core Qt6::Core)

# Unit test for PlateSolver::writeWcsIntoFits() -- the opt-in step behind
# `solve --update-fits-header` that copies a solved WCS (and SIP terms) from
# a .wcs sidecar into the original image's own FITS header, in place.
# Builds synthetic light-frame + .wcs fixtures (with and without SIP terms)
# so it has no dependency on solve-field being installed, and checks that
# OBJCTRA/OBJCTDEC are left untouched.
add_test(NAME fits_header_update_test COMMAND fits_header_update_test)

add_executable(linear_wcs_test linear_wcs_test.cpp)
target_link_libraries(linear_wcs_test PRIVATE epoch_from_core Qt6::Core)

# Unit test for LinearWcs's pixel->world math, cross-checked against
# astropy.wcs.WCS's output for the same (rotated, non-square) CD matrix.
add_test(NAME linear_wcs_test COMMAND linear_wcs_test)

add_executable(star_detector_test star_detector_test.cpp)
target_link_libraries(star_detector_test PRIVATE epoch_from_core Qt6::Core)

# Unit test for StarDetector against a synthetic image with known Gaussian
# stars at known sub-pixel positions -- entirely in-memory, no FITS file.
add_test(NAME star_detector_test COMMAND star_detector_test)

add_executable(equipment_calibrator_test equipment_calibrator_test.cpp)
target_link_libraries(equipment_calibrator_test PRIVATE epoch_from_core Qt6::Core)

# End-to-end integration test for the equipment-profiling pipeline: builds
# synthetic light frames + .wcs sidecars, injects a known order-2 distortion
# polynomial between a synthetic Gaia catalog and a fixed star grid's linear-
# WCS prediction, and checks the fit recovers it (order selection, before/
# after RMS, and the fitted polynomial evaluated at held-out points).
add_test(NAME equipment_calibrator_test COMMAND equipment_calibrator_test)

add_executable(image_dater_test image_dater_test.cpp)
target_link_libraries(image_dater_test PRIVATE epoch_from_core Qt6::Core)

# End-to-end integration test for ImageDater (the `date` CLI command's
# engine): renders one synthetic light frame + .wcs sidecar, then dates it
# two ways against two synthetic Gaia catalogs with known proper motions --
# without an equipment profile (trusting the image's own plain linear WCS)
# and with one (a known injected distortion, corrected via a synthetic
# EquipmentProfile) -- and checks both recover the same known "true" epoch,
# plus that the profile correction measurably reduces the residual over not
# applying it to the same distorted catalog.
add_test(NAME image_dater_test COMMAND image_dater_test)

add_executable(proper_motion_overlay_test proper_motion_overlay_test.cpp)
target_link_libraries(proper_motion_overlay_test PRIVATE epoch_from_core Qt6::Core)

# Unit test for ProperMotionOverlay::compute() (the Starfield tab's
# top-N-by-proper-motion / frame-bounds-filter / direction / length-scaling
# math) plus a light spot-check of ImageStretch::autoStretch(), against a
# synthetic .wcs fixture and hand-built Gaia stars whose expected pixel-
# space motion direction is derived independently from the WCS's own CD
# matrix convention.
add_test(NAME proper_motion_overlay_test COMMAND proper_motion_overlay_test)

add_executable(report_formatting_test report_formatting_test.cpp)
target_link_libraries(report_formatting_test PRIVATE epoch_from_core Qt6::Core)

# Unit test for combineDateEstimates() (the `date --dir` batch's
# inverse-variance-weighted average date): equal-weight combination lands
# at the midpoint with a tighter combined sigma, an unequal-weight
# combination is pulled toward the more precise estimate rather than
# averaged evenly, and failed/non-converged/rank-deficient/non-finite-sigma
# results are excluded rather than poisoning or dominating the average.
add_test(NAME report_formatting_test COMMAND report_formatting_test)
