@echo off
rem Builds EchoXR Hands with CMake and MSVC into out\ (the plugin, the OpenXR layer, the
rem settings window): run it from a Visual Studio x64 developer prompt, or let it find one.
setlocal
set "VARS_BAT="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VARS_BAT=%%i\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if not defined VSCMD_ARG_TGT_ARCH (
    if not defined VARS_BAT (
        echo [ERROR] MSVC not found.
        exit /b 1
    )
    call "%VARS_BAT%" >nul
)
cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
cmake --build build
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
echo.
echo Built into %~dp0out
