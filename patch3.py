import re

def patch_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    # Check if already patched
    if "totalRadianceBeforeBounce = totalRadiance;" in content.split("for (int bounce = 0; bounce < maxDepth; ++bounce) {")[1][:100]:
        print("Already patched.")
        return

    content = content.replace(
        "for (int bounce = 0; bounce < maxDepth; ++bounce) {\n        if (renderThread->IsStopRequested()) break;",
        "for (int bounce = 0; bounce < maxDepth; ++bounce) {\n        totalRadianceBeforeBounce = totalRadiance;\n        if (renderThread->IsStopRequested()) break;"
    )
    
    with open(filepath, 'w') as f:
        f.write(content)

patch_file("C:/Users/paolo/Desktop/code/hdGemini/renderer.cpp")
