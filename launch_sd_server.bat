@echo off
setlocal

set VENV_DIR=sd_venv

:: Check if the virtual environment exists, if not create it
if not exist %VENV_DIR% (
    echo [INFO] Creating Python virtual environment in %VENV_DIR%...
    python -m venv %VENV_DIR%
)

:: Activate the virtual environment
call %VENV_DIR%\Scripts\activate.bat

:: Install dependencies (only installs if missing)
echo [INFO] Ensuring required Python packages are installed...
:: We install the default PyTorch which supports CPU and NVIDIA GPUs
pip install torch torchvision
pip install diffusers transformers accelerate pillow PySide6

:: Run the server
echo [INFO] Launching Stable Diffusion Server...
python sd_server.py

pause
