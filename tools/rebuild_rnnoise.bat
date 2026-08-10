@echo off
setlocal
set "ROOT=%~dp0.."
if defined PLUGINGRAM_WORKSPACE set "ROOT=%PLUGINGRAM_WORKSPACE%"
if defined PLUGINGRAM_ROOT set "ROOT=%PLUGINGRAM_ROOT%"
set "LIBS_DIR=%ROOT%\Libraries\win64"
if defined PLUGINGRAM_LIBRARIES set "LIBS_DIR=%PLUGINGRAM_LIBRARIES%"
set "CC=cl"
set "CXX=cl"
cd /d "%LIBS_DIR%\rnnoise"
if exist out rmdir /S /Q out
mkdir out
cd out
cmake .. -G "Ninja Multi-Config" -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
if errorlevel 1 exit /b 1
cmake --build . --config Debug
if errorlevel 1 exit /b 1
cmake --build . --config Release
if errorlevel 1 exit /b 1
if not exist "Release\rnnoise.lib" (
  echo MISSING Release\rnnoise.lib
  dir /s /b *.lib
  exit /b 1
)
echo RNNOISE_OK
exit /b 0
