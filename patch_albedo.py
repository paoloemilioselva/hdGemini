import re

def patch_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    # 1. Fix outAlbedo
    old_albedo = """        if (bounce == 0) {
            if (outAlbedo) *outAlbedo = hit.baseColor;
            if (outNormal) *outNormal = hit.smoothNormal;
        }"""
    
    new_albedo = """        if (bounce == 0) {
            if (outAlbedo) {
                *outAlbedo = hit.baseColor;
                if (hit.transmission > 0.0f) {
                    *outAlbedo = hit.baseColor * (1.0f - hit.transmission) + hit.transmissionColor * hit.transmission;
                }
            }
            if (outNormal) *outNormal = hit.smoothNormal;
        }"""
        
    if old_albedo in content:
        content = content.replace(old_albedo, new_albedo)
        print("Patched outAlbedo logic.")

    with open(filepath, 'w') as f:
        f.write(content)

patch_file("C:/Users/paolo/Desktop/code/hdGemini/renderer.cpp")
