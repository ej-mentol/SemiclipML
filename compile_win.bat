@echo off
setlocal enabledelayedexpansion

:: --- Configuration ---
set "PROJECT_ROOT=%~dp0"
set "BUILD_DIR=%PROJECT_ROOT%build"
set "BIN_DIR=%PROJECT_ROOT%bin"
set "BUILD_TYPE=Release"

:: Suggested deploy path (Steam client dedicated server). SteamCMD installs
:: live wherever force_install_dir pointed, so this is only a suggestion --
:: the user is always asked, and the choice is saved to deploy_path.local.txt.
set "DEPLOY_SUGGEST=C:\Program Files (x86)\Steam\steamapps\common\Sven Co-op Dedicated Server\svencoop\addons\metamod\plugins"
set "DEPLOY_CONF=%PROJECT_ROOT%deploy_path.local.txt"
set "DEPLOY_PATH="

:: --- Submodule Check ---
:: A plain `git clone` (or GitHub "Download ZIP") leaves thirdparty/metamod-p
:: empty. Try to self-heal via git before failing with a clear message.
set "MMP_OK="
if exist "%PROJECT_ROOT%thirdparty\metamod-p\metamod\meta_api.h" set "MMP_OK=1"
if exist "%PROJECT_ROOT%thirdparty\metamod-p\include\metamod\meta_api.h" set "MMP_OK=1"
if not defined MMP_OK (
    echo [INFO] thirdparty/metamod-p is empty, initializing submodules...
    where git >nul 2>nul
    if errorlevel 1 (
        echo [ERROR] Submodule thirdparty/metamod-p is missing and git is not available.
        echo         If you downloaded this project as a ZIP, please clone it instead:
        echo         git clone --recursive https://github.com/ej-mentol/SemiclipML.git
        pause
        exit /b 1
    )
    git -C "%PROJECT_ROOT:~0,-1%" submodule update --init --recursive
)
set "MMP_OK="
if exist "%PROJECT_ROOT%thirdparty\metamod-p\metamod\meta_api.h" set "MMP_OK=1"
if exist "%PROJECT_ROOT%thirdparty\metamod-p\include\metamod\meta_api.h" set "MMP_OK=1"
if not defined MMP_OK (
    echo [ERROR] Failed to initialize submodule thirdparty/metamod-p.
    echo         Run manually: git submodule update --init --recursive
    pause
    exit /b 1
)

:: --- Deploy Path Selection ---
if exist "%DEPLOY_CONF%" (
    set /p DEPLOY_PATH=<"%DEPLOY_CONF%"
    echo [INFO] Using saved deploy path: "!DEPLOY_PATH!"
    echo        ^(delete deploy_path.local.txt to choose again^)
) else (
    if exist "%DEPLOY_SUGGEST%" (
        echo [INFO] Detected a Sven Co-op server at the default Steam location:
        echo        "%DEPLOY_SUGGEST%"
        set /p "USER_DEPLOY=Press Enter to use it, type a custom path, or '-' to skip deployment: "
        if "!USER_DEPLOY!"=="" set "USER_DEPLOY=%DEPLOY_SUGGEST%"
    ) else (
        echo [INFO] No server found at the default Steam location.
        echo        ^(SteamCMD servers live wherever force_install_dir pointed^)
        set /p "USER_DEPLOY=Enter your metamod plugins path, or press Enter to skip deployment: "
    )
    if "!USER_DEPLOY!"=="-" set "USER_DEPLOY="
    if not "!USER_DEPLOY!"=="" (
        set "USER_DEPLOY=!USER_DEPLOY:"=!"
        if exist "!USER_DEPLOY!" (
            set "DEPLOY_PATH=!USER_DEPLOY!"
            >"%DEPLOY_CONF%" echo(!USER_DEPLOY!
            echo [INFO] Deploy path saved to deploy_path.local.txt
        ) else (
            echo [WARNING] Path does not exist, skipping deployment: "!USER_DEPLOY!"
        )
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
    if exist "C:\Program Files\Microsoft Visual Studio\2026\Community" (
        set "VS_INSTALL_DIR=C:\Program Files\Microsoft Visual Studio\2026\Community"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2026\Professional" (
        set "VS_INSTALL_DIR=C:\Program Files\Microsoft Visual Studio\2026\Professional"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2026\Insiders" (
        set "VS_INSTALL_DIR=C:\Program Files\Microsoft Visual Studio\2026\Insiders"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community" (
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
:: Full clean build every time: the project is three .cpp files, a rebuild
:: costs seconds, and stale NMake object files have already produced
:: confusing LNK2019 errors after header/source updates.
if exist "%BUILD_DIR%" rmdir /S /Q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

:: --- Configure & Build ---
echo.
echo [INFO] Configuring CMake (!BUILD_TYPE!) in %BUILD_DIR%...
cd /d "%BUILD_DIR%"

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
if not "!DEPLOY_PATH!"=="" (
    if exist "!DEPLOY_PATH!" (
        echo.
        echo [INFO] Deploying to: "!DEPLOY_PATH!"
        copy /Y "%BIN_DIR%\SemiclipML.dll" "!DEPLOY_PATH!\"
        if !errorlevel! equ 0 (
            echo [INFO] Deployment successful.
        ) else (
            echo [ERROR] Deployment failed.
        )
    ) else (
        echo [WARNING] Deploy path does not exist: "!DEPLOY_PATH!"
    )
)

cd /d "%PROJECT_ROOT%"
echo.
echo [DONE] Build process finished.
pause
