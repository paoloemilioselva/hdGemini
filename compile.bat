@ECHO OFF

echo Building hdGemini...

set SYCL_FLAG=

REM Check for Intel oneAPI and initialize if present
if exist "C:\Program Files (x86)\Intel\oneAPI\setvars.bat" (
    echo [Gemini] Intel oneAPI found! Initializing SYCL environment...
    call "C:\Program Files (x86)\Intel\oneAPI\setvars.bat" > nul
    set CC=icx
    set CXX=icx
    set SYCL_FLAG=-DHDGEMINI_USE_SYCL=ON
) else (
    echo [Gemini] Intel oneAPI not found. Defaulting to CPU-only MSVC build.
)

if not exist build mkdir build
cd build
cmake -G "Ninja" -DTARGET_CONFIG=usd-26.03 -DCMAKE_BUILD_TYPE=RelWithDebInfo %SYCL_FLAG% ..
ninja install
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)
cd ..
echo Build and install successful.
