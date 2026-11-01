@echo off
rem Copyright (C) 2026 GZH Neo
rem Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
rem (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
rem setlocal: no variable of this script may stay in the console (CMake reads RC as the resource compiler).
setlocal
rem Pruefung des neuen Karteneditors unter Windows; Einzelheiten in pruefen_windows.ps1.
rem Weitere Schalter werden durchgereicht, etwa: pruefen_windows.cmd -OhneBau
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0pruefen_windows.ps1" %*
set SKRIPT_RC=%ERRORLEVEL%
if not defined PRUEFEN_OHNE_PAUSE pause
exit /b %SKRIPT_RC%
