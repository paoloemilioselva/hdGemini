import re

def patch_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    # 1. Fix p_hat_base calculation and base sample weight
    old_base_phat = """        float lumBase = 0.2126f * giVirtualRadiance[0] + 0.7152f * giVirtualRadiance[1] + 0.0722f * giVirtualRadiance[2];
        float p_hat_base = lumBase * (throughput[0] + throughput[1] + throughput[2]) / 3.0f; // Approx
        
        float randValBase = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
        r.Update(giVirtualLightPos, giVirtualLightNormal, GfVec3f(giVirtualRadiance[0], giVirtualRadiance[1], giVirtualRadiance[2]), (p_hat_base > 0.0f) ? 1.0f : 0.0f, randValBase); // M=1, w=1"""
    
    new_base_phat = """        float lumBase = 0.2126f * giVirtualRadiance[0] + 0.7152f * giVirtualRadiance[1] + 0.0722f * giVirtualRadiance[2];
        
        float p_hat_base = 0.0f;
        GfVec3f baseDir = giVirtualLightPos - giPrimaryPos;
        float baseDistSq = GfDot(baseDir, baseDir);
        if (baseDistSq > 1e-4f) {
            float baseDist = std::sqrt(baseDistSq);
            baseDir /= baseDist;
            float nDotL_base = std::max(0.0f, GfDot(giPrimaryNormal, baseDir));
            p_hat_base = lumBase * nDotL_base / baseDistSq;
        }
        
        // Calculate the Monte Carlo estimate luminance to find the unbiased ReSTIR weight
        float lumMC = 0.2126f * (giVirtualRadiance[0] * throughput[0]) + 
                      0.7152f * (giVirtualRadiance[1] * throughput[1]) + 
                      0.0722f * (giVirtualRadiance[2] * throughput[2]);
                      
        float w_base = (p_hat_base > 0.0f) ? (lumMC / p_hat_base) : 0.0f;
        
        float randValBase = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
        r.Update(giVirtualLightPos, giVirtualLightNormal, GfVec3f(giVirtualRadiance[0], giVirtualRadiance[1], giVirtualRadiance[2]), w_base, randValBase);"""
    
    content = content.replace(old_base_phat, new_base_phat)

    # 2. Fix the final totalRadiance addition
    old_final_add = """            for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                totalRadiance[i] += giPrimaryThroughput[i] * diffAlbedo[i] * giResRadiance[i] * (p_hat_final * finalDistSq / std::max(1e-6f, GfDot(giPrimaryNormal, finalDir))) * r.W;
            }"""
            
    new_final_add = """            for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                totalRadiance[i] += giPrimaryThroughput[i] * diffAlbedo[i] * giResRadiance[i] * (std::max(1e-6f, GfDot(giPrimaryNormal, finalDir)) / finalDistSq) * r.W;
            }"""
            
    content = content.replace(old_final_add, new_final_add)

    with open(filepath, 'w') as f:
        f.write(content)

patch_file("C:/Users/paolo/Desktop/code/hdGemini/renderer.cpp")
