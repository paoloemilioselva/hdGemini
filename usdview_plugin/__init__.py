from pxr import Tf, Usd, Sdf, UsdRender
from pxr.Usdviewq.plugin import PluginContainer

try:
    from PySide6 import QtWidgets, QtCore
except ImportError:
    try:
        from PySide2 import QtWidgets, QtCore
    except ImportError:
        QtWidgets = None

def createRenderSettings(usdviewApi):
    stage = usdviewApi.dataModel.stage
    if not stage:
        return
        
    rsPath = Sdf.Path("/Render/GeminiSettings")
    if stage.GetPrimAtPath(rsPath):
        print(f"RenderSettings already exists at {rsPath}")
        return
        
    # Create the RenderSettings prim
    rsPrim = UsdRender.Settings.Define(stage, rsPath)
    prim = rsPrim.GetPrim()
    
    def add_attr(name, typeName, default_val, display_name, doc=""):
        attr = prim.CreateAttribute(name, typeName)
        attr.Set(default_val)
        attr.SetCustomDataByKey("uiHints", {"displayName": display_name, "doc": doc})
        
    add_attr("gemini:enableSubsurface", Sdf.ValueTypeNames.Bool, True, "Enable Subsurface")
    add_attr("gemini:enableSubdivision", Sdf.ValueTypeNames.Bool, True, "Enable Subdivision")
    add_attr("gemini:targetSampleCount", Sdf.ValueTypeNames.Int, 32, "Target Sample Count")
    add_attr("gemini:maxReflectionBounces", Sdf.ValueTypeNames.Int, 8, "Max Reflection Bounces")
    add_attr("gemini:maxRefractionBounces", Sdf.ValueTypeNames.Int, 8, "Max Refraction Bounces")
    add_attr("gemini:enableDenoiser", Sdf.ValueTypeNames.Bool, True, "Enable Denoiser")
    add_attr("gemini:oceanEnable", Sdf.ValueTypeNames.Bool, False, "Enable Ocean")
    add_attr("gemini:oceanGridSize", Sdf.ValueTypeNames.Int, 128, "Ocean Grid Size")
    
    print(f"Created Gemini RenderSettings at {rsPath}")

def editSelectedPrim(usdviewApi):
    if not QtWidgets:
        print("PySide is not available.")
        return
        
    prims = usdviewApi.dataModel.selection.getPrims()
    if not prims:
        print("No prim selected.")
        return
        
    prim = prims[0]
    
    dialog = QtWidgets.QDialog(usdviewApi.qMainWindow)
    dialog.setWindowTitle(f"Edit Prim: {prim.GetName()}")
    layout = QtWidgets.QFormLayout(dialog)
    
    editors = []
    
    for attr in prim.GetAttributes():
        name = attr.GetName()
        custom_data = attr.GetCustomDataByKey("uiHints")
        display_name = name
        if custom_data and "displayName" in custom_data:
            display_name = custom_data["displayName"]
            
        val = attr.Get()
        
        if isinstance(val, bool):
            cb = QtWidgets.QCheckBox()
            cb.setChecked(val)
            layout.addRow(display_name, cb)
            editors.append((attr, cb, "bool"))
        elif isinstance(val, int):
            sb = QtWidgets.QSpinBox()
            sb.setRange(-999999, 999999)
            sb.setValue(val)
            layout.addRow(display_name, sb)
            editors.append((attr, sb, "int"))
        elif isinstance(val, float):
            dsb = QtWidgets.QDoubleSpinBox()
            dsb.setRange(-999999.0, 999999.0)
            dsb.setValue(val)
            layout.addRow(display_name, dsb)
            editors.append((attr, dsb, "float"))
        elif isinstance(val, str):
            le = QtWidgets.QLineEdit()
            le.setText(val)
            layout.addRow(display_name, le)
            editors.append((attr, le, "str"))
            
    buttons = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | QtWidgets.QDialogButtonBox.Cancel)
    buttons.accepted.connect(dialog.accept)
    buttons.rejected.connect(dialog.reject)
    layout.addRow(buttons)
    
    if dialog.exec() == QtWidgets.QDialog.Accepted:
        # Save values
        for attr, widget, type_ in editors:
            if type_ == "bool":
                attr.Set(widget.isChecked())
            elif type_ == "int":
                attr.Set(widget.value())
            elif type_ == "float":
                attr.Set(widget.value())
            elif type_ == "str":
                attr.Set(widget.text())

class GeminiPluginContainer(PluginContainer):
    def registerPlugins(self, plugRegistry, usdviewApi):
        self._createRenderSettings = plugRegistry.registerCommandPlugin(
            "GeminiPluginContainer.CreateRenderSettings",
            "Create Render Settings",
            createRenderSettings
        )
        self._editSelectedPrim = plugRegistry.registerCommandPlugin(
            "GeminiPluginContainer.EditSelectedPrim",
            "Edit Selected Prim",
            editSelectedPrim
        )

    def configureView(self, plugRegistry, plugUIBuilder):
        geminiMenu = plugUIBuilder.findOrCreateMenu("Gemini")
        geminiMenu.addItem(self._createRenderSettings)
        geminiMenu.addItem(self._editSelectedPrim)

Tf.Type.Define(GeminiPluginContainer)
