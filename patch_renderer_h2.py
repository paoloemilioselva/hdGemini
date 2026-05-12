import sys
import re

with open("renderer.h", "r") as f:
    content = f.read()

find_str = "    void _RenderTiles(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate);"
replace_str = """    void _RenderTiles(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate);
#ifdef HDGEMINI_HAS_SYCL
    void _RenderTilesSYCL(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate);
#endif"""
content = content.replace(find_str, replace_str)

with open("renderer.h", "w") as f:
    f.write(content)
print("renderer.h updated")