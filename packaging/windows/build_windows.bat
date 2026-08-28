@echo off
REM ===========================================================================
REM  Builds SourceGlo Pro for Windows. Run from the project root on a Windows
REM  machine (or the Parallels VM) with Visual Studio 2022 or newer
REM  ("Desktop development with C++") and CMake.
REM
REM  This is the route that does not need GitHub Actions at all - useful when
REM  the Actions storage quota is full, or when you just want a build now.
REM
REM  JUCE is used from ..\JUCE if present, and fetched automatically otherwise.
REM ===========================================================================

setlocal

where cmake >nul 2>nul || (
    echo ERROR: cmake is not on PATH. Install CMake and reopen this prompt.
    exit /b 1
)

echo == Configuring ==
REM No -G here: CMake picks the newest Visual Studio it can find. Pinning the
REM generator breaks the moment the machine moves to a newer Visual Studio.
cmake -B build-cmake -A x64 || exit /b 1

echo == Building (Release) ==
cmake --build build-cmake --config Release --parallel || exit /b 1

echo.
echo == Checking the artwork shipped ==
REM If the POST_BUILD copy lands anywhere else, every control in the plugin
REM silently falls back to a vector stand-in and only a screenshot would show it.
if not exist "build-cmake\SourceGloPro_artefacts\Release\VST3\SourceGlo Pro.vst3\Contents\Resources\Assets\Knobs\Filmstrips\knob_glo_280x280_128_vertical.png" (
    echo ERROR: artwork is missing from the VST3 bundle.
    exit /b 1
)
echo   artwork present

echo.
echo == Done ==
echo   VST3:       build-cmake\SourceGloPro_artefacts\Release\VST3\
echo   Standalone: build-cmake\SourceGloPro_artefacts\Release\Standalone\
echo.
echo The standalone reads its artwork from the Assets folder beside the .exe -
echo keep the two together or it opens with plain grey controls.
echo.
echo To build the installer as well, install Inno Setup 6 and run:
echo   "%%ProgramFiles(x86)%%\Inno Setup 6\ISCC.exe" packaging\windows\SourceGloPro.iss
echo.
echo To copy the VST3 into place for testing:
echo   xcopy /E /I /Y "build-cmake\SourceGloPro_artefacts\Release\VST3\SourceGlo Pro.vst3" "%%CommonProgramFiles%%\VST3\SourceGlo Pro.vst3"

endlocal
