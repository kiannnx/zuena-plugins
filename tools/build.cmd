@echo off
rem Compila ZUENA Plugins (Release) con MSVC + Ninja. Uso: tools\build.cmd [objetivo]
setlocal
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "ROOT=%~dp0.."
if not exist "%ROOT%\build\release\build.ninja" (
    "%CMAKE%" -S "%ROOT%" -B "%ROOT%\build\release" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
)
if "%~1"=="" (
    "%CMAKE%" --build "%ROOT%\build\release" --parallel
) else (
    "%CMAKE%" --build "%ROOT%\build\release" --parallel --target %1
)
