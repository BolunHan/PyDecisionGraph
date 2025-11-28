@echo off
set SPHINXBUILD=sphinx-build
set SOURCEDIR=source
set BUILDDIR=build

if "%1"=="clean" (
    rd /s /q %BUILDDIR%
    exit /b 0
)

%SPHINXBUILD% -M html "%SOURCEDIR%" "%BUILDDIR%"

