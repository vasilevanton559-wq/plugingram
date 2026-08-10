@echo off
setlocal
set "ROOT=%~dp0.."
if defined PLUGINGRAM_WORKSPACE set "ROOT=%PLUGINGRAM_WORKSPACE%"
if defined PLUGINGRAM_ROOT set "ROOT=%PLUGINGRAM_ROOT%"
set "LIBS_DIR=%ROOT%\Libraries\win64"
if defined PLUGINGRAM_LIBRARIES set "LIBS_DIR=%PLUGINGRAM_LIBRARIES%"
set "PATH=%ROOT%\ThirdParty\jom;%PATH%"
cd /d "%LIBS_DIR%\qt_5.15.19"

echo FINISH_QT_BUILD
jom -j%NUMBER_OF_PROCESSORS%
if errorlevel 1 (
  echo BUILD_RETRY_J1
  jom -j1
  if errorlevel 1 exit /b 1
)

echo FINISH_QT_INSTALL
jom -j%NUMBER_OF_PROCESSORS% install
if errorlevel 1 (
  echo INSTALL_RETRY_J1
  jom -j1 install
  if errorlevel 1 exit /b 1
)

echo QT_BUILD_OK
exit /b 0
