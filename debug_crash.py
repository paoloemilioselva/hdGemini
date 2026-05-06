import faulthandler
import os
import sys

# Enable faulthandler to get stack traces on C++ crashes
faulthandler.enable()

try:
    from pxr import Usd, UsdImagingGL, UsdAppUtils
    print("USD loaded successfully")
except ImportError as e:
    print(f"Failed to import USD: {e}")
    sys.exit(1)

scene_path = r"..\assets\full_assets\OpenChessSet\chess_set.usda"
print(f"Loading scene: {scene_path}")
stage = Usd.Stage.Open(scene_path)
if not stage:
    print("Failed to open stage")
    sys.exit(1)

print("Creating renderer...")
try:
    # UsdImagingGL.Engine creates the Hydra renderer
    engine = UsdImagingGL.Engine()
    engine.SetRendererPlugin("HdGeminiRendererPlugin")
    
    print("Setting camera...")
    camera_path = stage.GetDefaultPrim().GetPath() if stage.GetDefaultPrim() else "/camera"
    engine.SetCameraPath(camera_path)
    
    print("Rendering...")
    engine.Render(stage.GetPseudoRoot(), UsdImagingGL.RenderParams())
    print("Render finished.")
except Exception as e:
    print(f"Exception during rendering: {e}")
