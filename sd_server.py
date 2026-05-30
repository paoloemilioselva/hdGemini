import os
import sys
import time
import struct
import numpy as np
from PIL import Image
import torch
from diffusers import AutoPipelineForImage2Image

from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                               QHBoxLayout, QLabel, QScrollArea, QLineEdit, 
                               QPushButton, QDoubleSpinBox, QFrame)
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtCore import QTimer, Qt, Signal

REQUEST_FILE = "sd_request.bin"
RESPONSE_FILE = "sd_response.bin"

class ClickableLabel(QLabel):
    clicked = Signal()
    def mousePressEvent(self, event):
        self.clicked.emit()
        super().mousePressEvent(event)

class SDServer(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("SDXL-Turbo Debug Server")
        self.resize(900, 800)
        
        self.central_widget = QWidget()
        self.setCentralWidget(self.central_widget)
        self.main_layout = QVBoxLayout(self.central_widget)
        
        # Scroll area for history
        self.scroll_area = QScrollArea()
        self.scroll_area.setWidgetResizable(True)
        self.history_widget = QWidget()
        self.history_layout = QVBoxLayout(self.history_widget)
        self.history_layout.setAlignment(Qt.AlignTop)
        self.scroll_area.setWidget(self.history_widget)
        self.main_layout.addWidget(self.scroll_area)
        
        # Control panel at the bottom
        self.control_panel = QFrame()
        self.control_panel.setFrameShape(QFrame.StyledPanel)
        self.control_layout = QHBoxLayout(self.control_panel)
        
        self.prompt_input = QLineEdit()
        self.prompt_input.setPlaceholderText("Enter prompt...")
        self.control_layout.addWidget(QLabel("Prompt:"))
        self.control_layout.addWidget(self.prompt_input, stretch=1)
        
        self.strength_input = QDoubleSpinBox()
        self.strength_input.setRange(0.01, 1.0)
        self.strength_input.setSingleStep(0.05)
        self.strength_input.setValue(0.5)
        self.control_layout.addWidget(QLabel("Strength:"))
        self.control_layout.addWidget(self.strength_input)
        
        self.rerun_btn = QPushButton("Re-Run Diffusion")
        self.rerun_btn.clicked.connect(self.rerun_diffusion)
        self.control_layout.addWidget(self.rerun_btn)
        
        self.main_layout.addWidget(self.control_panel)
        
        # SD Pipeline
        print("Loading SDXL-Turbo model...")
        dtype = torch.float16
        variant = "fp16"
        if not torch.cuda.is_available():
            print("WARNING: CUDA not available. Falling back to CPU (will be slow).")
            dtype = torch.float32
            variant = None

        try:
            self.pipe = AutoPipelineForImage2Image.from_pretrained(
                "stabilityai/sdxl-turbo", 
                torch_dtype=dtype,
                variant=variant
            )
            if torch.cuda.is_available():
                self.pipe.to("cuda")
                
            # Memory optimizations
            self.pipe.enable_attention_slicing()
            print("Model loaded successfully!")
        except Exception as e:
            print(f"Error loading model: {e}")
            self.pipe = None
            
        self.last_buffer = None
        self.last_aovs = None
        
        self.timer = QTimer()
        self.timer.timeout.connect(self.check_for_requests)
        self.timer.start(100)
        print(f"Watching for {REQUEST_FILE}...")

    def check_for_requests(self):
        if not os.path.exists(REQUEST_FILE):
            return
            
        try:
            with open(REQUEST_FILE, "rb") as f:
                magic = struct.unpack("i", f.read(4))[0]
                if magic != 0x12345678:
                    print("Invalid magic number")
                    os.remove(REQUEST_FILE)
                    return
                
                width, height = struct.unpack("ii", f.read(8))
                prompt_len = struct.unpack("i", f.read(4))[0]
                prompt = f.read(prompt_len).decode("utf-8")
                strength = struct.unpack("f", f.read(4))[0]
                
                buffer_size = width * height * 4
                buffer_bytes = f.read(buffer_size * 4)
                buffer = np.frombuffer(buffer_bytes, dtype=np.float32).reshape((height, width, 4))
                
                aovs = {}
                try:
                    has_depth, has_normal, has_albedo = struct.unpack("iii", f.read(12))
                    if has_depth:
                        aovs['depth'] = np.frombuffer(f.read(buffer_size * 4), dtype=np.float32).reshape((height, width, 4))
                    if has_normal:
                        aovs['normal'] = np.frombuffer(f.read(buffer_size * 4), dtype=np.float32).reshape((height, width, 4))
                    if has_albedo:
                        aovs['albedo'] = np.frombuffer(f.read(buffer_size * 4), dtype=np.float32).reshape((height, width, 4))
                except struct.error:
                    pass
                
            os.remove(REQUEST_FILE)
            
            # Update UI inputs with received data
            self.prompt_input.setText(prompt)
            self.strength_input.setValue(strength)
            
            # Save state for reruns
            self.last_buffer = buffer
            self.last_aovs = aovs
            
            self.run_diffusion(prompt, strength, buffer, aovs)
            
        except Exception as e:
            print(f"Error processing request: {e}")
            if os.path.exists(REQUEST_FILE):
                os.remove(REQUEST_FILE)

    def rerun_diffusion(self):
        if self.last_buffer is None:
            print("No previous buffer to run on.")
            return
        prompt = self.prompt_input.text()
        strength = self.strength_input.value()
        self.run_diffusion(prompt, strength, self.last_buffer, self.last_aovs)

    def run_diffusion(self, prompt, strength, buffer_in, aovs_in):
        if self.pipe is None:
            print("Pipeline not loaded")
            return
            
        print(f"Processing prompt: '{prompt}', strength: {strength}")
        
        buffer = np.flipud(buffer_in.copy())
        aovs = {k: np.flipud(v.copy()) for k, v in aovs_in.items()}
        
        linear_rgb = np.clip(buffer[:, :, :3], 0.0, None)
        mapped_rgb = linear_rgb / (linear_rgb + 1.0)
        srgb = np.where(mapped_rgb <= 0.0031308, 
                       12.92 * mapped_rgb, 
                       1.055 * np.power(mapped_rgb, 1.0 / 2.4) - 0.055)
        
        rgb_uint8 = np.clip(srgb * 255.0, 0, 255).astype(np.uint8)
        init_image = Image.fromarray(rgb_uint8)
        
        original_size = init_image.size
        w, h = original_size
        scale = 512.0 / max(w, h)
        new_w = max(64, (int(w * scale) // 64) * 64)
        new_h = max(64, (int(h * scale) // 64) * 64)
        
        init_image_resized = init_image.resize((new_w, new_h), Image.LANCZOS)
        
        steps = max(4, int(2.0 / (strength + 0.001)))
        steps = min(10, steps)
        
        result = self.pipe(
            prompt=prompt, 
            image=init_image_resized, 
            num_inference_steps=steps,
            strength=strength, 
            guidance_scale=0.0
        ).images[0]
        
        result_restored = result.resize(original_size)
        result_srgb = np.array(result_restored).astype(np.float32) / 255.0
        
        result_linear_mapped = np.where(result_srgb <= 0.04045,
                                result_srgb / 12.92,
                                np.power((result_srgb + 0.055) / 1.055, 2.4))
        
        result_linear = result_linear_mapped / np.clip(1.0 - result_linear_mapped, 0.001, 1.0)
        
        out_buffer = np.ones((h, w, 4), dtype=np.float32)
        out_buffer[:, :, :3] = result_linear
        out_buffer = np.flipud(out_buffer)
        
        self.send_to_usdview(out_buffer)
        self.add_debug_row(init_image, result, prompt, strength, aovs, out_buffer)

    def send_to_usdview(self, out_buffer):
        with open(RESPONSE_FILE + ".tmp", "wb") as f:
            f.write(struct.pack("I", 0x87654321))
            f.write(out_buffer.tobytes())
        os.rename(RESPONSE_FILE + ".tmp", RESPONSE_FILE)
        print("Response sent to usdview (wiggle camera to force update if needed).")

    def add_debug_row(self, img_in, img_out, prompt, strength, aovs, raw_out_buffer):
        row_widget = QFrame()
        row_widget.setFrameShape(QFrame.Box)
        row_layout = QHBoxLayout(row_widget)
        
        def make_thumb(img, max_width=256):
            w, h = img.size
            if w > max_width:
                ratio = max_width / w
                return img.resize((max_width, int(h * ratio)), Image.LANCZOS)
            return img.copy()
            
        img_in_thumb = make_thumb(img_in, 256)
        img_out_thumb = make_thumb(img_out, 256)
        
        lbl_in = QLabel()
        qimg_in = QImage(img_in_thumb.tobytes(), img_in_thumb.width, img_in_thumb.height, QImage.Format_RGB888)
        lbl_in.setPixmap(QPixmap.fromImage(qimg_in))
        
        # Make the output image clickable
        lbl_out = ClickableLabel()
        lbl_out.setCursor(Qt.PointingHandCursor)
        lbl_out.setToolTip("Click to send this result back to usdview")
        # Keep a reference to the buffer so we can send it when clicked
        lbl_out.clicked.connect(lambda buf=raw_out_buffer: self.send_to_usdview(buf))
        
        qimg_out = QImage(img_out_thumb.tobytes(), img_out_thumb.width, img_out_thumb.height, QImage.Format_RGB888)
        lbl_out.setPixmap(QPixmap.fromImage(qimg_out))
        
        row_layout.addWidget(QLabel("Input:"))
        row_layout.addWidget(lbl_in)
        
        for name, aov_data in aovs.items():
            if name == 'depth':
                d = aov_data[:, :, 0]
                d = np.nan_to_num(d)
                d = np.clip(d / (np.max(d)+0.001) * 255.0, 0, 255).astype(np.uint8)
                aov_img = Image.fromarray(d).convert("RGB")
            else:
                c = np.clip(aov_data[:, :, :3], 0.0, 1.0)
                if name == 'normal':
                    c = (c + 1.0) * 0.5
                c = (c * 255.0).astype(np.uint8)
                aov_img = Image.fromarray(c)
                
            aov_thumb = make_thumb(aov_img, 256)
            lbl_aov = QLabel()
            qimg_aov = QImage(aov_thumb.tobytes(), aov_thumb.width, aov_thumb.height, QImage.Format_RGB888)
            lbl_aov.setPixmap(QPixmap.fromImage(qimg_aov))
            row_layout.addWidget(QLabel(name.capitalize() + ":"))
            row_layout.addWidget(lbl_aov)

        row_layout.addWidget(QLabel("Output (Click to send):"))
        row_layout.addWidget(lbl_out)
        
        info_str = f"Prompt: {prompt}\nStrength: {strength}"
        lbl_info = QLabel(info_str)
        lbl_info.setWordWrap(True)
        lbl_info.setMaximumWidth(150)
        row_layout.addWidget(lbl_info)
        
        self.history_layout.addWidget(row_widget)
        
        QTimer.singleShot(100, lambda: self.scroll_area.verticalScrollBar().setValue(self.scroll_area.verticalScrollBar().maximum()))

if __name__ == "__main__":
    app = QApplication(sys.argv)
    server = SDServer()
    server.show()
    sys.exit(app.exec())
