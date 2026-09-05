#!/usr/bin/env python3
"""
gaia_field_query.py
--------------------
Pull Gaia DR3 astrometry (position, proper motion, parallax) for every
reasonably well-measured star in a field around a given sky position, and
save it to a CSV. This is the "reference catalog" half of the proper-motion
image-dating pipeline: once you have this CSV and a plate-solved WCS for one
of your own frames, you can propagate each Gaia star's position to a trial
epoch and fit for the epoch that best matches what's actually in your image.

Requirements:
    pip install astropy astroquery

Usage examples:
    # Center the query using a FITS file's own header (reads CRVAL1/CRVAL2,
    # falling back to RA/DEC, falling back to OBJCTRA/OBJCTDEC):
    python gaia_field_query.py --fits "northamerica_Light_Ha_240_secs_001.fits" --out gaia_northamerica.csv

    # Or center it using an already-solved .wcs sidecar (from `EpochFrom
    # solve` or solve-field directly) -- reads the same CRVAL1/CRVAL2, so
    # this is the most reliable source when you have one, and doesn't
    # require re-parsing the (often much larger) light frame itself:
    python gaia_field_query.py --wcs "northamerica_Light_Ha_240_secs_001.wcs" --out gaia_northamerica.csv

    # Or give explicit coordinates (decimal degrees):
    python gaia_field_query.py --ra 314.925 --dec 43.664 --radius 0.9 --out gaia_northamerica.csv

    # Or use a known-good preset for this library (see PRESETS below) --
    # recommended for the 2013/2016/2017 sessions, where OBJCTRA/OBJCTDEC
    # has been observed to hold a stale target rather than the true frame
    # center:
    python gaia_field_query.py --target pelican --out gaia_pelican.csv

Notes:
    - Gaia DR3's reference epoch is 2016.0 (Julian year) for every row
      returned here -- that's the "t0" you propagate FROM using pmra/pmdec
      when fitting your image's unknown epoch.
    - pmra is already pmra*cos(dec) (i.e. it's a true angular rate on the
      sky), matching what astropy's SkyCoord.apply_space_motion() expects.
    - Units: pmra/pmdec in mas/yr, parallax in mas, ra/dec in degrees.
"""

import argparse
import sys

# Known-good field centers, taken from real plate solves (CRVAL1/CRVAL2) in
# this library's 2018 sessions -- safe to reuse for any session pointed at
# the same target, and a good fallback when a frame's own OBJCTRA/OBJCTDEC
# can't be trusted (see the 2017-08-28 / pelican_Light_clear session, where
# it was off by about 20 degrees in RA).
PRESETS = {
    "northamerica": (314.92508526, 43.664010339),
    "pelican": (312.918159238, 44.2244362737),
}


def get_center_from_fits(path):
    """Reads a field center out of a FITS header. Works equally well on an
    actual light frame's FITS file or on a solve-field/EpochFrom .wcs
    sidecar -- a .wcs file is itself a headers-only FITS file, so the same
    CRVAL1/CRVAL2 lookup applies to both (a sidecar just never falls through
    to the RA/DEC or OBJCTRA/OBJCTDEC branches below, since it never carries
    those capture-time keywords in the first place)."""
    from astropy.io import fits
    from astropy.coordinates import SkyCoord
    import astropy.units as u

    hdr = fits.getheader(path)

    # Best: a real plate-solved WCS center.
    if "CRVAL1" in hdr and "CRVAL2" in hdr:
        ra, dec = float(hdr["CRVAL1"]), float(hdr["CRVAL2"])
        print(f"Using CRVAL1/CRVAL2 (plate-solved WCS center): RA={ra:.5f} Dec={dec:.5f}")
        return ra, dec

    # Next best: decimal RA/DEC as recorded by the mount/capture software.
    if "RA" in hdr and "DEC" in hdr:
        try:
            ra, dec = float(hdr["RA"]), float(hdr["DEC"])
            print(f"Using RA/DEC header keys: RA={ra:.5f} Dec={dec:.5f}")
            return ra, dec
        except (TypeError, ValueError):
            pass

    # Last resort: sexagesimal target coordinates. These have been seen to
    # be STALE in at least one session in this library (2017-08-28 Pelican:
    # OBJCTRA/OBJCTDEC pointed ~20 degrees of RA away from the real field,
    # matching the *previous* target rather than the current one). Use with
    # real suspicion -- cross-check against the OBJECT name / filename, or
    # just pass --target/--ra/--dec explicitly instead.
    if "OBJCTRA" in hdr and "OBJCTDEC" in hdr:
        c = SkyCoord(hdr["OBJCTRA"], hdr["OBJCTDEC"], unit=(u.hourangle, u.deg))
        print(f"WARNING: no CRVAL/RA/DEC in header, falling back to OBJCTRA/OBJCTDEC. "
              f"This field has been seen to hold a STALE target in this library -- "
              f"double check RA={c.ra.deg:.5f} Dec={c.dec.deg:.5f} actually matches "
              f"'{hdr.get('OBJECT', '<no OBJECT key>')}' before trusting the result. "
              f"Prefer --target/--ra/--dec if in doubt.")
        return c.ra.deg, c.dec.deg

    raise ValueError(
        "Could not find CRVAL1/2, RA/DEC, or OBJCTRA/OBJCTDEC in this FITS header. "
        "Pass --ra/--dec or --target explicitly instead."
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--fits", help="A FITS file to read the field center from")
    src.add_argument("--wcs", help="An already-solved .wcs sidecar to read the field center from "
                                    "(reads CRVAL1/CRVAL2 -- same lookup as --fits, since a .wcs "
                                    "file is itself a headers-only FITS file)")
    src.add_argument("--ra", type=float, help="Field center RA, decimal degrees")
    src.add_argument("--target", choices=sorted(PRESETS), help="Use a known-good preset field center")
    ap.add_argument("--dec", type=float, help="Field center Dec, decimal degrees (required with --ra)")

    ap.add_argument("--radius", type=float, default=0.9,
                     help="Cone search radius in degrees (default 0.9, comfortably covers a "
                          "~4656x3520 ASI1600MM-Cool frame at ~800-1000mm focal length; "
                          "widen for a bigger sensor/shorter focal length)")
    ap.add_argument("--maglim", type=float, default=16.0,
                     help="Faintest Gaia G magnitude to include (default 16.0 -- loosen if your "
                          "subs are deep enough to centroid fainter stars, tighten to keep the "
                          "row count and query time down in rich Milky Way fields)")
    ap.add_argument("--max-ruwe", type=float, default=1.4,
                     help="Drop stars with RUWE above this (default 1.4, the standard Gaia "
                          "'well-behaved single-star astrometric solution' cut). Set to a large "
                          "number to disable.")
    ap.add_argument("--limit", type=int, default=50000,
                     help="Safety cap on returned rows (default 50000)")
    ap.add_argument("--out", required=True, help="Output CSV path")

    args = ap.parse_args()

    if args.ra is not None and args.dec is None:
        ap.error("--dec is required when using --ra")

    if args.fits:
        ra, dec = get_center_from_fits(args.fits)
    elif args.wcs:
        ra, dec = get_center_from_fits(args.wcs)
    elif args.target:
        ra, dec = PRESETS[args.target]
        print(f"Using preset '{args.target}': RA={ra:.5f} Dec={dec:.5f}")
    else:
        ra, dec = args.ra, args.dec

    from astroquery.gaia import Gaia

    # Deliberately no ORDER BY here -- some TAP backends (Gaia's included,
    # apparently) error out on an ORDER BY expression that isn't in the
    # SELECT list, especially combined with TOP. Sorting is done client-side
    # below instead, which is both more portable and avoids that whole class
    # of server-side bug.
    query = f"""
    SELECT TOP {args.limit}
        source_id, ref_epoch,
        ra, dec, ra_error, dec_error,
        pmra, pmra_error, pmdec, pmdec_error,
        parallax, parallax_error,
        phot_g_mean_mag, phot_bp_mean_mag, phot_rp_mean_mag,
        radial_velocity, radial_velocity_error,
        ruwe
    FROM gaiadr3.gaia_source
    WHERE CONTAINS(
        POINT('ICRS', ra, dec),
        CIRCLE('ICRS', {ra}, {dec}, {args.radius})
    ) = 1
    AND phot_g_mean_mag < {args.maglim}
    AND pmra IS NOT NULL
    AND pmdec IS NOT NULL
    AND ruwe < {args.max_ruwe}
    """

    print(f"\nQuerying Gaia DR3: center=({ra:.5f}, {dec:.5f}), radius={args.radius} deg, "
          f"G<{args.maglim}, RUWE<{args.max_ruwe} ...")
    print("(This submits an async job, polls until it finishes server-side, then downloads "
          "and parses the full result table -- nothing prints in between by default, which "
          "can look like a hang even when it's fine. verbose=True below shows the polling "
          "so you can tell it's alive. A dense field + faint maglim + wide radius can "
          "genuinely take a couple of minutes.)")

    try:
        # output_format="csv" avoids the VOTable-XML parsing overhead, which is the usual
        # culprit for a long *silent* pause right after the job reports as finished.
        job = Gaia.launch_job_async(query, output_format="csv", verbose=True)
        table = job.get_results()
    except Exception as e:
        print(f"\nGaia query failed: {e}")
        resp = getattr(e, "response", None)
        if resp is not None:
            print(f"HTTP status: {getattr(resp, 'status_code', '?')}")
            print(f"Response body (first 2000 chars):\n{getattr(resp, 'text', '')[:2000]}")
        print(
            "\nTroubleshooting:\n"
            "  - A bare 500 with no message is often transient on ESA's end -- try again.\n"
            "  - If it persists, try a smaller --limit (e.g. 2000) to rule out a size/timeout issue.\n"
            "  - Try --radius 0.3 to rule out a coordinate/region problem.\n"
            "  - Sanity check the field center this run used (printed above) against where the "
            "target actually is."
        )
        sys.exit(1)

    print(f"Got {len(table)} stars.")
    if len(table) == 0:
        print("No stars matched -- check your center coordinates and magnitude limit.")
        sys.exit(1)

    # Sort by total proper motion, fastest first -- client-side (see note above).
    table["pm_total_masyr"] = (table["pmra"] ** 2 + table["pmdec"] ** 2) ** 0.5
    table.sort("pm_total_masyr")
    table.reverse()

    print("\nFastest-moving stars in this field (mas/yr):")
    print(f"{'source_id':>20}  {'G mag':>6}  {'pmRA':>8}  {'pmDec':>8}  {'pm_total':>9}  {'parallax(mas)':>14}")
    for row in table[:10]:
        print(f"{row['source_id']:>20}  {row['phot_g_mean_mag']:>6.2f}  "
              f"{row['pmra']:>8.2f}  {row['pmdec']:>8.2f}  {row['pm_total_masyr']:>9.2f}  "
              f"{row['parallax']:>14.3f}")

    table.write(args.out, format="csv", overwrite=True)
    print(f"\nSaved {len(table)} rows to {args.out}")


if __name__ == "__main__":
    main()
