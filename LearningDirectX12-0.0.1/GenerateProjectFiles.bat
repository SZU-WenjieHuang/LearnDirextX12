@ECHO OFF

PUSHD %~dp0

REM Update these lines if the currently installed version of Visual Studio is not 2017.
SET CMAKE_GENERATOR="Visual Studio 17 2022 Win64"
SET CMAKE_BINARY_DIR=build_vs2022

MKDIR %CMAKE_BINARY_DIR% 2>NUL
PUSHD %CMAKE_BINARY_DIR%

cmake -G %CMAKE_GENERATOR% -Wno-dev "%~dp0"

POPD
POPD

PAUSE