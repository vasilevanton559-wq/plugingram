@echo off
setlocal enabledelayedexpansion
set "ROOT=%~dp0.."
if defined PLUGINGRAM_WORKSPACE set "ROOT=%PLUGINGRAM_WORKSPACE%"
if defined PLUGINGRAM_ROOT set "ROOT=%PLUGINGRAM_ROOT%"
set "LIBS_DIR=%ROOT%\Libraries\win64"
if defined PLUGINGRAM_LIBRARIES set "LIBS_DIR=%PLUGINGRAM_LIBRARIES%"
set "QT=5.15.19"
set "ANGLE_DIR=%LIBS_DIR%\tg_angle"
set "ANGLE_LIBS_DIR=%ANGLE_DIR%\out"
set "MOZJPEG_DIR=%LIBS_DIR%\mozjpeg"
set "OPENSSL_DIR=%LIBS_DIR%\openssl3"
set "OPENSSL_LIBS_DIR=%OPENSSL_DIR%\out"
set "ZLIB_LIBS_DIR=%LIBS_DIR%\zlib"
set "WEBP_DIR=%LIBS_DIR%\libwebp"

cd /d "%LIBS_DIR%\qt_%QT%"
if exist config.cache del /f /q config.cache
if exist "%LIBS_DIR%\Qt-%QT%" rmdir /Q /S "%LIBS_DIR%\Qt-%QT%"

call configure -prefix "%LIBS_DIR%\Qt-%QT%" ^
 -debug-and-release ^
 -force-debug-info ^
 -opensource ^
 -confirm-license ^
 -static ^
 -static-runtime ^
 -opengl es2 -no-angle ^
 -I "%ANGLE_DIR%\include" ^
 -D "KHRONOS_STATIC=" ^
 -D "DESKTOP_APP_QT_STATIC_ANGLE=" ^
 QMAKE_LIBS_OPENGL_ES2_DEBUG="%ANGLE_LIBS_DIR%\Debug\tg_angle.lib %ZLIB_LIBS_DIR%\Debug\libzsd.lib d3d9.lib dxgi.lib dxguid.lib" ^
 QMAKE_LIBS_OPENGL_ES2_RELEASE="%ANGLE_LIBS_DIR%\Release\tg_angle.lib %ZLIB_LIBS_DIR%\Release\libzs.lib d3d9.lib dxgi.lib dxguid.lib" ^
 -egl ^
 QMAKE_LIBS_EGL_DEBUG="%ANGLE_LIBS_DIR%\Debug\tg_angle.lib %ZLIB_LIBS_DIR%\Debug\libzsd.lib d3d9.lib dxgi.lib dxguid.lib Gdi32.lib User32.lib" ^
 QMAKE_LIBS_EGL_RELEASE="%ANGLE_LIBS_DIR%\Release\tg_angle.lib %ZLIB_LIBS_DIR%\Release\libzs.lib d3d9.lib dxgi.lib dxguid.lib Gdi32.lib User32.lib" ^
 -openssl-linked ^
 -I "%OPENSSL_DIR%\include" ^
 OPENSSL_LIBS_DEBUG="%OPENSSL_LIBS_DIR%.dbg\libssl.lib %OPENSSL_LIBS_DIR%.dbg\libcrypto.lib Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib" ^
 OPENSSL_LIBS_RELEASE="%OPENSSL_LIBS_DIR%\libssl.lib %OPENSSL_LIBS_DIR%\libcrypto.lib Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib" ^
 -I "%MOZJPEG_DIR%" ^
 LIBJPEG_LIBS_DEBUG="%MOZJPEG_DIR%\Debug\jpeg-static.lib" ^
 LIBJPEG_LIBS_RELEASE="%MOZJPEG_DIR%\Release\jpeg-static.lib" ^
 -system-webp ^
 -I "%WEBP_DIR%\src" ^
 -L "%WEBP_DIR%\out\release-static\x64\lib" ^
 -mp ^
 -no-feature-netlistmgr ^
 -nomake examples ^
 -nomake tests ^
 -platform win32-msvc
if errorlevel 1 exit /b 1

jom -j%NUMBER_OF_PROCESSORS% || jom -j%NUMBER_OF_PROCESSORS%
if errorlevel 1 exit /b 1
jom -j%NUMBER_OF_PROCESSORS% install
if errorlevel 1 exit /b 1
echo QT_BUILD_OK
exit /b 0
