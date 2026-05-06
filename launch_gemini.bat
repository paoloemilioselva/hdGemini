@ECHO OFF

SET USDROOT=C:\dev\usd-26.03
SET RMANTREE=C:\Program Files\Pixar\RenderManProServer-26.3
SET USDEXTRA=C:\Users\paolo\Desktop\usd-26.03-extra

SET RMAN_SHADERPATH=%RMANTREE%\lib\shaders;%USDROOT%\plugin\usd\resources\shaders
SET RMAN_RIXPLUGINPATH=%RMANTREE%\lib\plugins
SET RMAN_TEXTUREPATH=%RMANTREE%\lib\textures:%RMANTREE%\lib\plugins:%USDROOT%\plugin\usd
SET RMAN_DISPLAYPATH=%RMANTREE%\lib\plugins
SET RMAN_PROCEDURALPATH=%RMANTREE%\lib\plugins

SET PXR_PLUGINPATH_NAME=%USDROOT%;%USDROOT%\plugin\usd;%USDEXTRA%\plugin\usd

SET PYTHONPATH=%USDROOT%\lib\python;%USDEXTRA%\lib\python;%PYTHONPATH%

SET PATH=%USDROOT%\bin;%USDEXTRA%\bin;%RMANTREE%\bin;%PATH%
SET PATH=%USDROOT%\lib;%USDEXTRA%\lib;%RMANTREE%\lib;%PATH%

SET PXR_DEBUG=*

SET SCENE_FILE=%1
IF "%SCENE_FILE%"=="" SET SCENE_FILE=..\assets\full_assets\OpenChessSet\chess_set.usda

echo Launching usdview with Gemini renderer and scene: %SCENE_FILE%
usdview %SCENE_FILE% --renderer Gemini
