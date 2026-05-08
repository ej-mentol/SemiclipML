@echo off
setlocal enabledelayedexpansion

:: --- Configuration ---
set "PROJECT_ROOT=%~dp0"
set "BUILD_DIR=%PROJECT_ROOT%build"
set "BIN_DIR=%PROJECT_ROOT%bin"
set "BUILD_TYPE=Release"

:: Default deploy path (Sven Co-op server)
set "DEPLOY_PATH=C:\Program Files (x86)\Steam\steamapps\common\Sven Co-op Dedicated Server\svencoop\addons\metamod\plugins"

:: --- Deploy Path Check ---
if not exist "%DEPLOY_PATH%" (
    echo [WARNING] Default deploy path not found: "%DEPLOY_PATH%"
    set /p "USER_DEPLOY=Enter custom deploy path (or press Enter to skip deployment): "
    if not "!USER_DEPLOY!"=="" (
        set "DEPLOY_PATH=!USER_DEPLOY!"
    ) else (
        set "DEPLOY_PATH="
    )
)

:: --- Find Visual Studio ---
echo [INFO] Searching for Visual Studio...

:: 1. Try vswhere (most reliable)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL_DIR=%%i"
    )
)

:: 2. Fallback to common paths if vswhere failed or didn't find anything
if "!VS_INSTALL_DIR!"=="" (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community" (
        set "VS_INSTALL_DIR=C:\Program Files\Microsoft Visual Studio\2022\Community"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community" (
        set "VS_INSTALL_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community" (
        set "VS_INSTALL_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2017\Community"
    )
)

if "!VS_INSTALL_DIR!"=="" (
    echo [ERROR] Could not detect Visual Studio installation.
    echo Please edit compile_win.bat and set VS_INSTALL_DIR manually.
    pause
    exit /b 1
)

echo [INFO] Using VS at: %VS_INSTALL_DIR%

:: --- Initialize Environment ---
set "VCVARS=%VS_INSTALL_DIR%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvarsall.bat not found at "%VCVARS%".
    pause
    exit /b 1
)

:: Initialize x86 environment (standard for Half-Life plugins)
call "%VCVARS%" x86 >nul

:: --- Clean / Prepare Directories ---
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

:: --- Configure & Build ---
echo.
echo [INFO] Configuring CMake (!BUILD_TYPE!) in %BUILD_DIR%...
cd /d "%BUILD_DIR%"
if exist "CMakeCache.txt" del "CMakeCache.txt"

cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=!BUILD_TYPE! ..
if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed.
    pause
    exit /b %errorlevel%
)

echo.
echo [INFO] Building...
nmake
if %errorlevel% neq 0 (
    echo [ERROR] Build failed.
    pause
    exit /b %errorlevel%
)

:: --- Copy Artifacts ---
echo.
echo [INFO] Copying artifacts...
if exist "SemiclipML.dll" (
    copy /Y "SemiclipML.dll" "%BIN_DIR%\" >nul
)
if exist "SemiclipML.pdb" (
    copy /Y "SemiclipML.pdb" "%BIN_DIR%\" >nul
)

echo [INFO] Build successful. Output: %BIN_DIR%\SemiclipML.dll

:: --- Deploy ---
if not "%DEPLOY_PATH%"=="" (
    if exist "%DEPLOY_PATH%" (
        echo.
        echo [INFO] Deploying to: "%DEPLOY_PATH%"
        copy /Y "%BIN_DIR%\SemiclipML.dll" "%DEPLOY_PATH%\"
        if %errorlevel% equ 0 (
            echo [INFO] Deployment successful.
        ) else (
            echo [ERROR] Deployment failed.
        )
    ) else (
        echo [WARNING] Deploy path does not exist: "%DEPLOY_PATH%"
    )
)

cd /d "%PROJECT_ROOT%"
echo.
echo [DONE] Build process finished.
pause
