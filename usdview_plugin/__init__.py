from pxr import Tf, Usd, Sdf, UsdRender, Gf
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
        
    # Use DefinePrim instead of UsdRender.Settings.Define to avoid applying Pxr schemas
    # and just create a clean prim.
    prim = stage.DefinePrim(rsPath, "RenderSettings")
    
    def add_attr(name, typeName, default_val, display_name, display_group="General", doc=""):
        attr = prim.CreateAttribute(name, typeName)
        attr.Set(default_val)
        attr.SetCustomDataByKey("uiHints", {"displayName": display_name, "displayGroup": display_group, "doc": doc})
        
    add_attr("gemini:renderAlbedoOnly", Sdf.ValueTypeNames.Bool, False, "Render Albedo Only (Debug)", "General")
    add_attr("gemini:enableGenAi", Sdf.ValueTypeNames.Bool, False, "Generative AI: Enable", "General")
    add_attr("gemini:genAiPrompt", Sdf.ValueTypeNames.String, "", "Generative AI: Prompt", "Generative AI")
    add_attr("gemini:genAiStrength", Sdf.ValueTypeNames.Float, 0.5, "Generative AI: Strength", "Generative AI")
    add_attr("gemini:renderLightGeometry", Sdf.ValueTypeNames.Bool, True, "Render Light Geometry", "General")
    add_attr("gemini:enableSubsurface", Sdf.ValueTypeNames.Bool, True, "Enable Subsurface Scattering", "General")
    add_attr("gemini:enableSubdivision", Sdf.ValueTypeNames.Bool, True, "Enable Subdivision", "General")
    add_attr("gemini:enableDenoiser", Sdf.ValueTypeNames.Bool, True, "Enable OIDN Denoiser", "Post-Processing")
    add_attr("gemini:enableFireflyFilter", Sdf.ValueTypeNames.Bool, True, "Enable Pre-Pass: Firefly Filter", "Post-Processing")
    add_attr("gemini:enableChromaticityBlur", Sdf.ValueTypeNames.Bool, True, "Enable Pre-Pass: Chromaticity Blur", "Post-Processing")
    add_attr("gemini:targetSampleCount", Sdf.ValueTypeNames.Int, 32, "Target Sample Count", "Sampling")
    add_attr("gemini:enableRestirDI", Sdf.ValueTypeNames.Bool, True, "Enable ReSTIR Direct Illumination", "Sampling")
    add_attr("gemini:maxReflectionBounces", Sdf.ValueTypeNames.Int, 8, "Max Reflection Bounces", "Sampling")
    add_attr("gemini:maxRefractionBounces", Sdf.ValueTypeNames.Int, 8, "Max Refraction Bounces", "Sampling")
    add_attr("gemini:resolutionLevel", Sdf.ValueTypeNames.Int, 2, "Resolution Level", "Sampling")
    add_attr("gemini:antiAliasingFilter", Sdf.ValueTypeNames.Int, 1, "Anti-Aliasing Filter (0=None, 1=Box, 2=Tent, 3=Gaussian)", "Sampling")
    add_attr("gemini:enableDoF", Sdf.ValueTypeNames.Bool, False, "Enable DoF", "Camera")
    add_attr("gemini:enableSycl", Sdf.ValueTypeNames.Bool, True, "Enable SYCL GPU Acceleration", "System")
    add_attr("gemini:enableOnScreenStats", Sdf.ValueTypeNames.Bool, False, "Enable On-Screen Stats", "System")
    add_attr("gemini:focalLength", Sdf.ValueTypeNames.Float, 50.0, "Focal Length (mm)", "Camera")
    add_attr("gemini:fStop", Sdf.ValueTypeNames.Float, 5.6, "F-Stop (Aperture)", "Camera")
    add_attr("gemini:focusDistance", Sdf.ValueTypeNames.Float, 10.0, "Focus Distance", "Camera")
    add_attr("gemini:bokehBlades", Sdf.ValueTypeNames.Int, 0, "Bokeh Blades", "Camera")
    add_attr("gemini:enablePhysicalCamera", Sdf.ValueTypeNames.Bool, False, "Override Physical Camera Parameters", "Camera")
    add_attr("gemini:iso", Sdf.ValueTypeNames.Float, 100.0, "ISO", "Camera")
    add_attr("gemini:shutterSpeed", Sdf.ValueTypeNames.Float, 0.02, "Shutter Speed", "Camera")
    add_attr("gemini:enableLensFlare", Sdf.ValueTypeNames.Bool, False, "Enable Lens Flare", "Camera")
    add_attr("gemini:renderIblBackground", Sdf.ValueTypeNames.Bool, True, "Render IBL Background", "Physical Sky")
    add_attr("gemini:lensDistortion", Sdf.ValueTypeNames.Float, 0.0, "Lens Distortion", "Camera")
    add_attr("gemini:chromaticAberration", Sdf.ValueTypeNames.Float, 0.0, "Chromatic Aberration", "Camera")
    add_attr("gemini:physicalSkyEnable", Sdf.ValueTypeNames.Bool, False, "Enable Physical Sky", "Physical Sky")
    add_attr("gemini:physicalSkyAzimuth", Sdf.ValueTypeNames.Float, 0.0, "Physical Sky Azimuth", "Physical Sky")
    add_attr("gemini:physicalSkyAltitude", Sdf.ValueTypeNames.Float, 90.0, "Physical Sky Altitude", "Physical Sky")
    add_attr("gemini:physicalSkySunExposure", Sdf.ValueTypeNames.Float, 0.0, "Physical Sky Sun Exposure", "Physical Sky")
    add_attr("gemini:physicalSkySkyExposure", Sdf.ValueTypeNames.Float, 0.0, "Physical Sky Sky Exposure", "Physical Sky")
    add_attr("gemini:volumeStepSize", Sdf.ValueTypeNames.Float, 0.1, "Volume Step Size", "Volume")
    add_attr("gemini:volumeDensityScale", Sdf.ValueTypeNames.Float, 1.0, "Volume Density Scale", "Volume")
    add_attr("gemini:enableAdaptiveSampling", Sdf.ValueTypeNames.Bool, True, "Enable Adaptive Sampling", "Sampling")
    add_attr("gemini:adaptiveVarianceThreshold", Sdf.ValueTypeNames.Float, 0.01, "Adaptive Variance Threshold", "Sampling")
    add_attr("gemini:adaptiveMinSamples", Sdf.ValueTypeNames.Int, 16, "Adaptive Min Samples", "Sampling")
    add_attr("gemini:oceanEnable", Sdf.ValueTypeNames.Bool, False, "Ocean Enable", "Ocean")
    add_attr("gemini:oceanDicingScale", Sdf.ValueTypeNames.Float, 10.0, "Ocean Dicing Scale", "Ocean")
    add_attr("gemini:oceanContinuousDicing", Sdf.ValueTypeNames.Bool, False, "Ocean Continuous Dicing", "Ocean")
    add_attr("gemini:oceanWaterHeight", Sdf.ValueTypeNames.Float, 0.0, "Ocean Water Height", "Ocean")
    add_attr("gemini:oceanGridSize", Sdf.ValueTypeNames.Int, 128, "Ocean Grid Size", "Ocean")
    add_attr("gemini:oceanTime", Sdf.ValueTypeNames.Float, 0.0, "Ocean Time", "Ocean")
    add_attr("gemini:oceanSize", Sdf.ValueTypeNames.Float, 100.0, "Ocean Size", "Ocean")
    add_attr("gemini:oceanAmplitude1", Sdf.ValueTypeNames.Float, 0.0, "Ocean Amplitude 1", "Ocean")
    add_attr("gemini:oceanAmplitude2", Sdf.ValueTypeNames.Float, 0.0, "Ocean Amplitude 2", "Ocean")
    add_attr("gemini:oceanAmplitude3", Sdf.ValueTypeNames.Float, 0.0, "Ocean Amplitude 3", "Ocean")
    add_attr("gemini:oceanChoppiness1", Sdf.ValueTypeNames.Float, 1.2, "Ocean Choppiness 1", "Ocean")
    add_attr("gemini:oceanChoppiness2", Sdf.ValueTypeNames.Float, 1.2, "Ocean Choppiness 2", "Ocean")
    add_attr("gemini:oceanChoppiness3", Sdf.ValueTypeNames.Float, 1.2, "Ocean Choppiness 3", "Ocean")
    add_attr("gemini:oceanStrength1", Sdf.ValueTypeNames.Float, 1.0, "Ocean Strength 1", "Ocean")
    add_attr("gemini:oceanStrength2", Sdf.ValueTypeNames.Float, 1.0, "Ocean Strength 2", "Ocean")
    add_attr("gemini:oceanStrength3", Sdf.ValueTypeNames.Float, 1.0, "Ocean Strength 3", "Ocean")
    add_attr("gemini:oceanFoamVisibility", Sdf.ValueTypeNames.Float, 1.0, "Ocean Foam Visibility", "Ocean")
    add_attr("gemini:oceanWindSpeed1", Sdf.ValueTypeNames.Float, 0.0, "Ocean Wind Speed 1", "Ocean")
    add_attr("gemini:oceanWindSpeed2", Sdf.ValueTypeNames.Float, 0.0, "Ocean Wind Speed 2", "Ocean")
    add_attr("gemini:oceanWindSpeed3", Sdf.ValueTypeNames.Float, 0.0, "Ocean Wind Speed 3", "Ocean")
    add_attr("gemini:oceanWindDirectionX1", Sdf.ValueTypeNames.Float, 1.0, "Ocean Wind Direction X 1", "Ocean")
    add_attr("gemini:oceanWindDirectionX2", Sdf.ValueTypeNames.Float, 1.0, "Ocean Wind Direction X 2", "Ocean")
    add_attr("gemini:oceanWindDirectionX3", Sdf.ValueTypeNames.Float, 1.0, "Ocean Wind Direction X 3", "Ocean")
    add_attr("gemini:oceanWindDirectionY1", Sdf.ValueTypeNames.Float, 1.0, "Ocean Wind Direction Y 1", "Ocean")
    add_attr("gemini:oceanWindDirectionY2", Sdf.ValueTypeNames.Float, 1.0, "Ocean Wind Direction Y 2", "Ocean")
    add_attr("gemini:oceanWindDirectionY3", Sdf.ValueTypeNames.Float, 1.0, "Ocean Wind Direction Y 3", "Ocean")
    add_attr("gemini:oceanMinK1", Sdf.ValueTypeNames.Float, 0.0, "Ocean Min K 1", "Ocean")
    add_attr("gemini:oceanMinK2", Sdf.ValueTypeNames.Float, 0.0, "Ocean Min K 2", "Ocean")
    add_attr("gemini:oceanMinK3", Sdf.ValueTypeNames.Float, 0.0, "Ocean Min K 3", "Ocean")
    add_attr("gemini:oceanMaxK1", Sdf.ValueTypeNames.Float, 1000000.0, "Ocean Max K 1", "Ocean")
    add_attr("gemini:oceanMaxK2", Sdf.ValueTypeNames.Float, 1000000.0, "Ocean Max K 2", "Ocean")
    add_attr("gemini:oceanMaxK3", Sdf.ValueTypeNames.Float, 1000000.0, "Ocean Max K 3", "Ocean")
    add_attr("gemini:oceanDisableShader", Sdf.ValueTypeNames.Bool, False, "Ocean Disable Shader", "Ocean")
    add_attr("gemini:oceanRepeat", Sdf.ValueTypeNames.Bool, True, "Ocean Repeat", "Ocean")
    add_attr("gemini:oceanScatteringColor", Sdf.ValueTypeNames.Float3, Gf.Vec3f(0.02, 0.15, 0.25), "Ocean Scattering Color", "Ocean")
    add_attr("gemini:oceanScatteringDepth", Sdf.ValueTypeNames.Float, 10.0, "Ocean Scattering Depth", "Ocean")
    add_attr("gemini:meniscusSize", Sdf.ValueTypeNames.Float, 0.015, "Camera Waterline Meniscus Size", "Camera")
    add_attr("gemini:meniscusBend", Sdf.ValueTypeNames.Float, 0.2, "Camera Waterline Meniscus Bend", "Camera")
    add_attr("gemini:meniscusTint", Sdf.ValueTypeNames.Float3, Gf.Vec3f(0.02, 0.05, 0.04), "Camera Waterline Meniscus Tint", "Camera")
    add_attr("gemini:metersPerUnit", Sdf.ValueTypeNames.Float, 0.01, "System Meters Per Unit", "System")

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
    dialog.resize(500, 600)
    
    main_layout = QtWidgets.QVBoxLayout(dialog)
    tab_widget = QtWidgets.QTabWidget()
    main_layout.addWidget(tab_widget)
    
    # Split attributes by namespace
    # namespace -> list of attributes
    tabs_data = {}
    
    for attr in prim.GetAttributes():
        name = attr.GetName()
        parts = name.split(':')
        
        namespace = "General"
        if len(parts) > 1:
            namespace = parts[0]
            
        if namespace not in tabs_data:
            tabs_data[namespace] = []
        tabs_data[namespace].append(attr)
        
    class CollapsibleBox(QtWidgets.QWidget):
        def __init__(self, title="", parent=None):
            super().__init__(parent)
            self.toggle_button = QtWidgets.QToolButton(text=title, checkable=True, checked=True)
            self.toggle_button.setStyleSheet("QToolButton { border: none; font-weight: bold; text-align: left; }")
            self.toggle_button.setToolButtonStyle(QtCore.Qt.ToolButtonTextBesideIcon)
            self.toggle_button.setArrowType(QtCore.Qt.DownArrow)
            self.toggle_button.toggled.connect(self.on_toggled)
            self.toggle_button.setSizePolicy(QtWidgets.QSizePolicy.MinimumExpanding, QtWidgets.QSizePolicy.Fixed)

            self.content_area = QtWidgets.QWidget()
            self.content_layout = QtWidgets.QFormLayout(self.content_area)
            self.content_layout.setContentsMargins(15, 5, 0, 10)

            lay = QtWidgets.QVBoxLayout(self)
            lay.setSpacing(0)
            lay.setContentsMargins(0, 5, 0, 0)
            lay.addWidget(self.toggle_button)
            lay.addWidget(self.content_area)

        def on_toggled(self, checked):
            self.toggle_button.setArrowType(QtCore.Qt.DownArrow if checked else QtCore.Qt.RightArrow)
            self.content_area.setVisible(checked)
            
        def addRow(self, label, widget):
            self.content_layout.addRow(label, widget)

    for ns, attrs in sorted(tabs_data.items()):
        tab = QtWidgets.QWidget()
        
        scroll = QtWidgets.QScrollArea()
        scroll.setWidgetResizable(True)
        scroll_widget = QtWidgets.QWidget()
        scroll_layout = QtWidgets.QVBoxLayout(scroll_widget)
        scroll_layout.setAlignment(QtCore.Qt.AlignTop)
        
        # Group attributes by displayGroup
        groups = {}
        for attr in attrs:
            custom_data = attr.GetCustomDataByKey("uiHints")
            group_name = "General"
            if custom_data and "displayGroup" in custom_data and custom_data["displayGroup"]:
                group_name = custom_data["displayGroup"]
            if group_name not in groups:
                groups[group_name] = []
            groups[group_name].append(attr)
            
        for group_name, group_attrs in sorted(groups.items()):
            group_box = CollapsibleBox(group_name)
            scroll_layout.addWidget(group_box)
            
            for attr in group_attrs:
                name = attr.GetName()
                custom_data = attr.GetCustomDataByKey("uiHints")
                display_name = name
                if custom_data and "displayName" in custom_data:
                    display_name = custom_data["displayName"]
                    
                val = attr.Get()
                
                if isinstance(val, bool):
                    cb = QtWidgets.QCheckBox()
                    cb.setChecked(val)
                    cb.toggled.connect(lambda v, a=attr: a.Set(v))
                    group_box.addRow(display_name, cb)
                elif isinstance(val, int):
                    sb = QtWidgets.QSpinBox()
                    sb.setRange(-999999, 999999)
                    sb.setValue(val)
                    sb.valueChanged.connect(lambda v, a=attr: a.Set(v))
                    group_box.addRow(display_name, sb)
                elif isinstance(val, float):
                    dsb = QtWidgets.QDoubleSpinBox()
                    dsb.setRange(-999999.0, 999999.0)
                    dsb.setValue(val)
                    dsb.valueChanged.connect(lambda v, a=attr: a.Set(v))
                    group_box.addRow(display_name, dsb)
                elif isinstance(val, str):
                    le = QtWidgets.QLineEdit()
                    le.setText(val)
                    le.textChanged.connect(lambda v, a=attr: a.Set(v))
                    group_box.addRow(display_name, le)
                elif isinstance(val, Gf.Vec3f):
                    w = QtWidgets.QWidget()
                    l = QtWidgets.QHBoxLayout(w)
                    l.setContentsMargins(0,0,0,0)
                    s1 = QtWidgets.QDoubleSpinBox(); s1.setRange(-9999.0, 9999.0); s1.setValue(val[0])
                    s2 = QtWidgets.QDoubleSpinBox(); s2.setRange(-9999.0, 9999.0); s2.setValue(val[1])
                    s3 = QtWidgets.QDoubleSpinBox(); s3.setRange(-9999.0, 9999.0); s3.setValue(val[2])
                    
                    def update_vec3f(val_unused, a=attr, v1=s1, v2=s2, v3=s3):
                        a.Set(Gf.Vec3f(v1.value(), v2.value(), v3.value()))
                    s1.valueChanged.connect(update_vec3f)
                    s2.valueChanged.connect(update_vec3f)
                    s3.valueChanged.connect(update_vec3f)
                    
                    l.addWidget(s1); l.addWidget(s2); l.addWidget(s3)
                    group_box.addRow(display_name, w)
                    
        scroll.setWidget(scroll_widget)
        tab_layout = QtWidgets.QVBoxLayout(tab)
        tab_layout.addWidget(scroll)
        tab_widget.addTab(tab, ns)
    # Workaround: Force Update by toggling active state
    force_update_btn = QtWidgets.QPushButton("Force Update")
    def force_update():
        prim.SetActive(False)
        prim.SetActive(True)
    force_update_btn.clicked.connect(force_update)
    main_layout.addWidget(force_update_btn)

    # Save dialog reference to avoid garbage collection
    usdviewApi.qMainWindow._gemini_prim_editor = dialog
    dialog.show()

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
