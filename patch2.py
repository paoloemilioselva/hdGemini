import re

def patch_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    # Revert accidental replacement in _TraceShadowRay
    content = content.replace("while (distRemaining > 0.0f && bounce < 8) {\n        totalRadianceBeforeBounce = totalRadiance;", "while (distRemaining > 0.0f && bounce < 8) {")
    
    # Correct replacement in _TraceRay
    content = content.replace("while (distRemaining > 0.0f && bounce < _maxBounces) {", "while (distRemaining > 0.0f && bounce < _maxBounces) {\n        totalRadianceBeforeBounce = totalRadiance;")
    
    with open(filepath, 'w') as f:
        f.write(content)

patch_file("C:/Users/paolo/Desktop/code/hdGemini/renderer.cpp")
