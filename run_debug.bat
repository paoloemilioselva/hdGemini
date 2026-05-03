@ECHO OFF

SET USDROOT=C:\dev\usd-26.03
SET RMANTREE=C:\Program Files\Pixar\RenderManProServer-26.3
SET USDEXTRA=C:\Users\paolo\Desktop\usd-26.03-extra

SET PXR_PLUGINPATH_NAME=%USDROOT%;%USDROOT%\plugin\usd;%USDEXTRA%\plugin\usd
SET PYTHONPATH=%USDROOT%\lib\python;%USDEXTRA%\lib\python;%PYTHONPATH%

SET PATH=%USDROOT%\bin;%USDEXTRA%\bin;%RMANTREE%\bin;%PATH%
SET PATH=%USDROOT%\lib;%USDEXTRA%\lib;%RMANTREE%\lib;%PATH%

python debug_crash.py