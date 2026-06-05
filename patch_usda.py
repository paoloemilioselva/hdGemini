import glob

usda_files = glob.glob("gallery/*.usda")

injection = """        custom bool gemini:enableSubsurface = True (
            uiHints = {
                string displayName = "Enable Subsurface Scattering"
                string displayGroup = "Performance"
            }
        )
        custom bool gemini:enableRestirGI = True (
            uiHints = {
                string displayName = "Enable ReSTIR GI"
                string displayGroup = "Performance"
            }
        )
"""

for f in usda_files:
    with open(f, 'r') as file:
        content = file.read()
    
    if "gemini:enableRestirGI" in content:
        continue
        
    # Find the def RenderSettings "GeminiRenderSettings" block
    search_str = 'def RenderSettings "GeminiRenderSettings"\n    {\n'
    if search_str in content:
        content = content.replace(search_str, search_str + injection)
        with open(f, 'w') as file:
            file.write(content)
        print(f"Patched {f}")
    else:
        # Might use a different brace style
        search_str2 = 'def RenderSettings "GeminiRenderSettings" {\n'
        if search_str2 in content:
            content = content.replace(search_str2, search_str2 + injection)
            with open(f, 'w') as file:
                file.write(content)
            print(f"Patched {f}")
        else:
            print(f"Could not find RenderSettings in {f}")
