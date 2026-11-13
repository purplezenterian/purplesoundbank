@echo off
rem Installs PSB Bridge for your user (no admin needed).
set DEST=%LOCALAPPDATA%\Programs\Common\VST3
if not exist "%DEST%" mkdir "%DEST%"
xcopy /E /I /Y "%~dp0PSB Bridge.vst3" "%DEST%\PSB Bridge.vst3" >nul
echo.
echo PSB Bridge installed. Restart your DAW and rescan plugins.
pause
