@echo off
REM Wrapper that lets EpochFrom (or any other native Windows app that spawns
REM a process directly via CreateProcess, as Qt's QProcess does) invoke
REM ansvr's solve-field.
REM
REM Why this is needed: ansvr installs astrometry.net's own solve-field,
REM which is a Perl script, not a compiled .exe -- it only runs inside the
REM Cygwin environment ansvr bundles. CreateProcess can't execute a
REM Perl-shebang script at all (that's a Unix kernel feature Windows has no
REM equivalent of), so pointing EpochFrom's "solve-field path" setting
REM directly at solve-field (with or without a trailing .exe) fails with
REM "failed to start ... -- is astrometry.net installed and on PATH?" no
REM matter how correct the path is. Confirmed on a real Windows/ansvr setup:
REM routing the call through Cygwin's own bash.exe, as this script does, is
REM what actually gets it running.
REM
REM Setup: copy this file anywhere convenient, edit ANSVR_HOME below if your
REM ansvr install isn't in the default location, and point EpochFrom's
REM "solve-field path" setting (Solve tab, or --solve-field-path on the
REM CLI) at *this .bat file* instead of at solve-field itself. Qt's
REM QProcess knows how to launch .bat/.cmd files on Windows (it shells out
REM via cmd.exe automatically), so this drops straight in as a substitute
REM "solve-field".
REM
REM Argument forwarding: EARLIER versions of this script did
REM   bash.exe --login -c "solve-field %*"
REM which jams the whole %* expansion into a SINGLE already-quoted -c
REM string. That string then gets parsed twice with two different sets of
REM quoting rules -- once by Windows' own CreateProcess argv parser
REM building bash.exe's command line, then again by bash's own -c string
REM word-splitting -- and an image path containing a space (a very normal
REM thing for astrophotography capture software's own auto-naming, e.g.
REM "2017-04-29 21-15-33 M31.fits") silently breaks apart between the two,
REM truncating the filename before astrometry.net ever sees the rest of it.
REM Confirmed on a real ansvr run.
REM
REM The fix: keep the -c script itself fixed and tiny (just "exec
REM solve-field \"$@\"", which forwards bash's own positional parameters
REM verbatim, each one still intact as its own argument), and pass the
REM real arguments as bash -c's own *trailing* arguments instead of
REM string-splicing them into the script text. Those trailing arguments
REM are parsed exactly once, using Windows' standard argv-quoting rules
REM (the same rules Qt's QProcess used to build them in the first place),
REM so a space inside one of them survives intact.

set "ANSVR_HOME=%LOCALAPPDATA%\cygwin_ansvr"

"%ANSVR_HOME%\bin\bash.exe" --login -c "exec solve-field \"$@\"" bash %*
