@echo off
chcp 65001 >nul
title BloodStrike ESP — Build Kernel Driver
echo ============================================
echo  BloodStrike ESP — Build Driver (.sys)
echo  Solo linea de comandos, sin Visual Studio
echo ============================================
echo.

rem ----------------------------------------------------------
rem Check if cl.exe is already available
rem ----------------------------------------------------------
where cl.exe >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] Compilador MSVC encontrado.
    set BUILD_TOOLS_OK=1
) else (
    echo [*] No se encontro cl.exe. Instalando VS Build Tools...
    echo    (Esto NO es Visual Studio, es solo el compilador ~600MB)
    set BUILD_TOOLS_OK=0
)

rem ----------------------------------------------------------
rem Check if WDK headers are available (pick newest SDK)
rem ----------------------------------------------------------
set WDK_FOUND=0
for /f "tokens=*" %%i in ('dir /b /o-n "%ProgramFiles(x86)%\Windows Kits\10\Include\10.0.*" 2^>nul') do (
    if exist "%ProgramFiles(x86)%\Windows Kits\10\Include\%%i\km\ntddk.h" (
        set WDK_FOUND=1
        set SDK_VER=%%i
        goto :wdk_checked
    )
)
:wdk_checked

if %WDK_FOUND% equ 1 (
    echo [OK] WDK encontrado (SDK %SDK_VER%^)
) else (
    echo [*] WDK no encontrado. Instalando...
)

echo.

rem ----------------------------------------------------------
rem Install VS Build Tools if needed
rem ----------------------------------------------------------
if %BUILD_TOOLS_OK% equ 0 (
    echo === Paso 1: Instalando VS Build Tools (solo compilador, ~600MB) ===
    echo Esto descarga e instala cl.exe + link.exe sin el IDE de Visual Studio.
    echo.
    winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
    if %errorlevel% neq 0 (
        echo.
        echo [ERROR] Fallo la instalacion de VS Build Tools.
        echo Intenta descargar manualmente desde:
        echo https://visualstudio.microsoft.com/es/downloads/#build-tools-for-visual-studio-2022
        pause
        exit /b 1
    )
    echo [OK] VS Build Tools instalados.
    echo.
)

rem ----------------------------------------------------------
rem Install WDK if needed
rem ----------------------------------------------------------
if %WDK_FOUND% equ 0 (
    echo === Paso 2: Instalando Windows WDK (~1-2GB) ===
    echo Esto instala los headers y librerias para compilar drivers de kernel.
    echo.
    winget install Microsoft.WindowsWDK --accept-source-agreements --accept-package-agreements
    if %errorlevel% neq 0 (
        echo.
        echo [ERROR] Fallo la instalacion del WDK.
        echo Intenta descargar manualmente desde:
        echo https://go.microsoft.com/fwlink/?linkid=2263961
        pause
        exit /b 1
    )
    
    rem Re-check for WDK headers after install (pick newest SDK)
    for /f "tokens=*" %%i in ('dir /b /o-n "%ProgramFiles(x86)%\Windows Kits\10\Include\10.0.*" 2^>nul') do (
        if exist "%ProgramFiles(x86)%\Windows Kits\10\Include\%%i\km\ntddk.h" (
            set WDK_FOUND=1
            set SDK_VER=%%i
            goto :wdk_rechecked
        )
    )
    :wdk_rechecked
    echo [OK] WDK instalado (SDK %SDK_VER%^).
    echo.
)

rem ----------------------------------------------------------
rem Locate the latest SDK with kernel-mode headers
rem ----------------------------------------------------------
if %WDK_FOUND% equ 0 (
    echo [ERROR] No se encontraron los headers del WDK.
    echo Asegurate de tener el Windows SDK instalado.
    pause
    exit /b 1
)

echo === Paso 3: Compilando driver ===
echo.

rem Setup VS Build Tools environment
set VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\BuildTools
if exist "%VS_PATH%\Common7\Tools\VsDevCmd.bat" (
    call "%VS_PATH%\Common7\Tools\VsDevCmd.bat"
) else (
    rem Try enterprise/professional paths as fallback
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
    ) else (
        if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" (
            call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
        )
    )
)

set WDK_ROOT=%ProgramFiles(x86)%\Windows Kits\10
set KM_INC=%WDK_ROOT%\Include\%SDK_VER%\km
set SHARED_INC=%WDK_ROOT%\Include\%SDK_VER%\shared
set UM_INC=%WDK_ROOT%\Include\%SDK_VER%\um
set KM_LIB=%WDK_ROOT%\Lib\%SDK_VER%\km\x64

echo SDK Version: %SDK_VER%
echo Include: %KM_INC%
echo Lib: %KM_LIB%
echo.

mkdir build_driver 2>nul

echo Compilando driver.c...
cl.exe /nologo /c /I. /I"%KM_INC%" /I"%SHARED_INC%" /I"%UM_INC%" ^
    /D"DRIVER" /D"_AMD64_" /D"_WIN32_WINNT=0x0A00" /GS- /O2 /Oi /arch:AVX2 ^
    Kernel\driver.c /Fobuild_driver\driver.obj

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Compilacion fallida.
    pause
    exit /b 1
)
echo [OK] Compilacion exitosa.
echo.

echo Linkeando BS_KernelDriver.sys...
link.exe /NOLOGO /OUT:build_driver\BS_KernelDriver.sys ^
    /MANIFEST:NO /DRIVER /SUBSYSTEM:NATIVE /OPT:REF /OPT:ICF ^
    /MACHINE:X64 /ENTRY:DriverEntry build_driver\driver.obj ^
    /LIBPATH:"%KM_LIB%" ntoskrnl.lib hal.lib

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Linkeo fallido.
    pause
    exit /b 1
)

echo.
echo ============================================
echo  DRIVER COMPILADO EXITOSAMENTE!
echo ============================================
echo.
echo  Archivo: build_driver\BS_KernelDriver.sys
echo.
echo  Para cargar el driver:
echo    sc create BS_KernelDriver type=kernel binPath="%CD%\build_driver\BS_KernelDriver.sys"
echo    sc start BS_KernelDriver
echo.
pause
