@echo off
setlocal

set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if not exist %VCVARS% (
    set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
)
if not exist %VCVARS% (
    echo Visual Studio 2022 not found. Install Build Tools or Visual Studio with the C++ workload.
    exit /b 1
)

call %VCVARS% x64
if errorlevel 1 exit /b 1

set CMAKE="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist %CMAKE% set CMAKE=cmake

set ROOT=%~dp0..
if not exist "%ROOT%\build" mkdir "%ROOT%\build"
pushd "%ROOT%\build"

%CMAKE% .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

nmake
if errorlevel 1 exit /b 1

copy /Y platform\win32\UmbraNote.exe ..\UmbraNote.exe >nul
echo.
echo Build successful: %ROOT%\UmbraNote.exe
popd
endlocal