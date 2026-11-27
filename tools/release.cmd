@echo off
rem Publica una versión nueva de ZUENA EQ en el launcher local.
rem 1) Compila el plugin  2) Empaqueta con la VERSION de plugins\ZuenaEQ\CMakeLists.txt
rem 3) Reempaqueta el launcher  4) Lo copia adonde apunta el acceso directo del escritorio.
setlocal
call "%~dp0build.cmd" || exit /b 1
pushd "%~dp0..\Free-VST-Client" || exit /b 1
call npm run release
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%
