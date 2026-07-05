@echo off
setlocal

set ROOT=%~dp0..
set BASH="C:\Program Files\Git\bin\bash.exe"
if not exist %BASH% set BASH=bash

pushd "%ROOT%"
%BASH% -lc "scripts/sign-release.sh"
set EXITCODE=%ERRORLEVEL%
popd
endlocal & exit /b %EXITCODE%