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
REM %* forwards every argument EpochFrom passes (--cpulimit, --axy, --ra,
REM the image path, etc.) through to solve-field unchanged, inside a single
REM bash -c string -- which is also the documented pattern other Windows
REM astronomy apps (SGP and others) use to call into ansvr/Cygwin-hosted
REM astrometry.net tools.

set "ANSVR_HOME=%LOCALAPPDATA%\cygwin_ansvr"

"%ANSVR_HOME%\bin\bash.exe" --login -c "solve-field %*"
