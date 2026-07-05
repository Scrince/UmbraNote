@echo off
setlocal

set ROOT=%~dp0..
set BASH="C:\Program Files\Git\bin\bash.exe"
if not exist %BASH% set BASH=bash

pushd "%ROOT%"
%BASH% -lc "scripts/init-release-key.sh"
set EXITCODE=%ERRORLEVEL%
popd
endlocal & exit /b %EXITCODE%