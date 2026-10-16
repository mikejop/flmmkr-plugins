@echo off
REM NOT TESTED - prepared for a future Windows build.
REM Run from an elevated prompt. Expects build\FalseColor.ofx.bundle next to this script's parent.
set SRC=%~dp0..\build\FalseColor.ofx.bundle
set DEST=C:\Program Files\Common Files\OFX\Plugins
if not exist "%SRC%" ( echo Bundle not found: %SRC% & exit /b 1 )
xcopy /E /I /Y "%SRC%" "%DEST%\FalseColor.ofx.bundle"
echo Installed to %DEST%\FalseColor.ofx.bundle. Restart DaVinci Resolve.
