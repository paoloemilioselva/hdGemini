#ifndef HD_GEMINI_FIELD_H
#define HD_GEMINI_FIELD_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/field.h"
#include "pxr/usd/sdf/assetPath.h"

#ifdef HDGEMINI_HAS_NANOVDB
#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/GridHandle.h>
#endif

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiField final : public HdField {
public:
    HdGeminiField(SdfPath const& id, TfToken const& fieldType);
    virtual ~HdGeminiField() = default;

    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam   *renderParam,
                      HdDirtyBits     *dirtyBits) override;

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    const SdfAssetPath& GetFilePath() const { return _filePath; }
    const TfToken& GetFieldName() const { return _fieldName; }

#ifdef HDGEMINI_HAS_NANOVDB
    const nanovdb::FloatGrid* GetNanoVDBGrid() const { return _grid; }
#endif

private:
    SdfAssetPath _filePath;
    TfToken _fieldName;
    
#ifdef HDGEMINI_HAS_NANOVDB
    nanovdb::GridHandle<> _gridHandle;
    const nanovdb::FloatGrid* _grid = nullptr;
#endif
};

#endif // HD_GEMINI_FIELD_H
