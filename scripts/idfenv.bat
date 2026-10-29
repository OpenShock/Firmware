@echo off
rem Puts an installer-registered ESP-IDF on this shell's environment (cmd.exe).
rem
rem     call scripts\idfenv.bat
rem
rem Also usable as a CLion toolchain "environment file", so every CMake profile sees ESP-IDF.
rem The environment comes from scripts\utils\idfenv.py --dump (KEY=VALUE lines).
for /f "usebackq delims=" %%l in (`python "%~dp0utils\idfenv.py" --dump`) do set "%%l"
if errorlevel 1 (
  echo idfenv: no ESP-IDF install found - run the ESP-IDF export script instead 1>&2
  exit /b 1
)
set "PYTHON="
