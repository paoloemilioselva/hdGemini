#include "field.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include <iostream>

#ifdef HDGEMINI_HAS_NANOVDB
#include <nanovdb/util/IO.h>
#endif

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiField::HdGeminiField(SdfPath const& id, TfToken const& fieldType)
    : HdField(id)
{
}

HdDirtyBits HdGeminiField::GetInitialDirtyBitsMask() const {
    return HdField::DirtyParams;
}

void HdGeminiField::Sync(HdSceneDelegate *sceneDelegate,
                         HdRenderParam   *renderParam,
                         HdDirtyBits     *dirtyBits)
{
    if (*dirtyBits & HdField::DirtyParams) {
        VtValue fileVal = sceneDelegate->Get(GetId(), HdFieldTokens->filePath);
        if (fileVal.IsHolding<SdfAssetPath>()) {
            _filePath = fileVal.Get<SdfAssetPath>();
            
#ifdef HDGEMINI_HAS_NANOVDB
            if (!_filePath.GetResolvedPath().empty()) {
                try {
                    // Read the nanovdb grid from the file
                    // OpenVDB files can contain multiple grids, so we might need the fieldName
                    VtValue nameVal = sceneDelegate->Get(GetId(), HdFieldTokens->fieldName);
                    std::string gridName = "";
                    if (nameVal.IsHolding<TfToken>()) {
                        gridName = nameVal.Get<TfToken>().GetString();
                    }
                    
                    _gridHandle = nanovdb::io::readGrid(_filePath.GetResolvedPath(), gridName);
                    if (_gridHandle.gridMetaData()->gridClass() == nanovdb::GridClass::FogVolume ||
                        _gridHandle.gridMetaData()->gridClass() == nanovdb::GridClass::Unknown) {
                        _grid = _gridHandle.grid<float>();
                        std::cout << "[Gemini] Loaded NanoVDB grid: " << _filePath.GetResolvedPath() << " (" << gridName << ")" << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[Gemini] Failed to load NanoVDB grid: " << e.what() << std::endl;
                    _grid = nullptr;
                }
            }
#endif
        }
    }
    
    *dirtyBits = HdField::Clean;
}
