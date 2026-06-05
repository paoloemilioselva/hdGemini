import re

def patch_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    # 1. Fix outAlbedo
    old_albedo = """        if (intersected && bounce == 0 && outAlbedo) {
            *outAlbedo = hit.baseColor;
        }"""
    
    new_albedo = """        if (intersected && bounce == 0 && outAlbedo) {
            *outAlbedo = hit.baseColor;
            if (hit.transmission > 0.0f) {
                *outAlbedo = hit.baseColor * (1.0f - hit.transmission) + hit.transmissionColor * hit.transmission;
            }
        }"""
        
    if old_albedo in content:
        content = content.replace(old_albedo, new_albedo)
        print("Patched outAlbedo logic.")

    # 2. Fix SSS vs Transmission
    old_sss = """                    if (transmitted) {
                        currentRayDir = refractDir;
                        currentRayOrigin = hitPos - n * RAY_EPSILON(hitPos);
                        if (isInside) {
                            if (mediumStackCount > 1) mediumStackCount--;
                        } else {
                            if (mediumStackCount < 16) mediumStack[mediumStackCount++] = MediumState{hit.ior, hit.transmissionScatter, hit.transmissionDepth, hit.transmissionColor};
                        }
                        
                        if (remainingProb >= sssProb) {
                            // It is Transmission
                            throughput = throughput * RGBToSpectrum(hit.transmissionColor, lambda);
                        }
                    } else {"""
                    
    new_sss = """                    if (transmitted) {
                        currentRayDir = refractDir;
                        currentRayOrigin = hitPos - n * RAY_EPSILON(hitPos);
                        if (isInside) {
                            if (mediumStackCount > 1) mediumStackCount--;
                        } else {
                            if (remainingProb >= sssProb) {
                                // It is Transmission
                                if (mediumStackCount < 16) mediumStack[mediumStackCount++] = MediumState{hit.ior, hit.transmissionScatter, hit.transmissionDepth, hit.transmissionColor};
                            } else {
                                // It is SSS
                                // For SSS, subsurfaceColor acts as scatter color, subsurfaceScale acts as depth
                                if (mediumStackCount < 16) mediumStack[mediumStackCount++] = MediumState{hit.ior, hit.subsurfaceColor, hit.subsurfaceScale, hit.baseColor};
                            }
                        }
                        
                        if (remainingProb >= sssProb) {
                            // It is Transmission
                            throughput = throughput * RGBToSpectrum(hit.transmissionColor, lambda);
                        }
                    } else {"""

    if old_sss in content:
        content = content.replace(old_sss, new_sss)
        print("Patched SSS vs Transmission logic.")
    
    with open(filepath, 'w') as f:
        f.write(content)

patch_file("C:/Users/paolo/Desktop/code/hdGemini/renderer.cpp")
