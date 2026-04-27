@ECHO OFF

echo Building hdGemini...
if not exist build mkdir build
cd build
cmake -G "Ninja" -DTARGET_CONFIG=usd-26.03 -DCMAKE_BUILD_TYPE=Release ..
ninja install
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)
cd ..
echo Build and install successful.
