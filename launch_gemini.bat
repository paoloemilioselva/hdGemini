@ECHO OFF
SET CURRENT_FOLDER=%~dp0
CALL ..\..\usd-26.03_env.bat
REM The above CALL starts a new cmd.exe at the end because of the batch file content provided.
REM We might need to adjust or just manually type the commands.
REM However, for automation, let's assume we can run the logic here.

echo Building hdGemini...
mkdir build
cd build
cmake -G "Ninja" -DTARGET_CONFIG=usd-26.03 ..
ninja install
cd ..

echo Launching usdview with Gemini renderer...
usdview scene.usda --renderer Gemini
