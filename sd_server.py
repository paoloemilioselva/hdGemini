import torch
import time
import os
import struct
import numpy as np
import sys
from diffusers import AutoPipelineForImage2Image
from PIL import Image

# PySide6 imports
from PySide6.QtWidgets import QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLabel, QScrollArea, QFrame
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtCore import QTimer, Qt

REQUEST_FILE = "sd_request.bin"
RESPONSE_FILE = "sd_response.bin"

class DebugServerWindow(QWidget):
    def __init__(self, pipe):
        super().__init__()
        self.pipe = pipe
        self.setWindowTitle("SD Turbo Debug Server")
        self.resize(600, 800)

        main_layout = QVBoxLayout(self)

        self.scroll_area = QScrollArea()
        self.scroll_area.setWidgetResizable(True)
        
        self.container = QWidget()
        self.grid_layout = QVBoxLayout(self.container)
        self.grid_layout.setAlignment(Qt.AlignTop)
        
        self.scroll_area.setWidget(self.container)
        main_layout.addWidget(self.scroll_area)
        
        # Timer to poll for requests
        self.poll_timer = QTimer(self)
        self.poll_timer.timeout.connect(self.check_request)
        self.poll_timer.start(100)
        
        print(f"Watching for {REQUEST_FILE}...")

    def check_request(self):
        if not os.path.exists(REQUEST_FILE):
            return
            
        try:
            # Wait briefly to ensure C++ has finished writing
            time.sleep(0.1)
            
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
                
                # Read float buffer (width * height * 4 floats)
                buffer_size = width * height * 4
                buffer_bytes = f.read(buffer_size * 4)
                buffer = np.frombuffer(buffer_bytes, dtype=np.float32).reshape((height, width, 4))
                
                # Check if there are extra AOVs
                aovs = {}
                try:
                    has_depth, has_normal, has_albedo = struct.unpack("iii", f.read(12))
                    if has_depth:
                        depth_bytes = f.read(buffer_size * 4)
                        aovs['depth'] = np.frombuffer(depth_bytes, dtype=np.float32).reshape((height, width, 4))
                    if has_normal:
                        normal_bytes = f.read(buffer_size * 4)
                        aovs['normal'] = np.frombuffer(normal_bytes, dtype=np.float32).reshape((height, width, 4))
                    if has_albedo:
                        albedo_bytes = f.read(buffer_size * 4)
                        aovs['albedo'] = np.frombuffer(albedo_bytes, dtype=np.float32).reshape((height, width, 4))
                except struct.error:
                    pass # Older version of the plugin, ignore
                
            # Delete request file so we don't process it again
            os.remove(REQUEST_FILE)
            
            print(f"Processing prompt: '{prompt}', strength: {strength}")
            
            # 1. Flip Y (USD has origin at bottom-left, PIL expects top-left)
            buffer = np.flipud(buffer)
            for k in aovs:
                aovs[k] = np.flipud(aovs[k])
            
            # 2. Convert from Linear HDR to LDR sRGB for the AI model
            # Stable diffusion is trained on sRGB images!
            linear_rgb = np.clip(buffer[:, :, :3], 0.0, None)
            
            # Simple Reinhard tone mapping to handle HDR brightness
            mapped_rgb = linear_rgb / (linear_rgb + 1.0)
            
            srgb = np.where(mapped_rgb <= 0.0031308, 
                           12.92 * mapped_rgb, 
                           1.055 * np.power(mapped_rgb, 1.0 / 2.4) - 0.055)
            
            rgb_uint8 = np.clip(srgb * 255.0, 0, 255).astype(np.uint8)
            init_image = Image.fromarray(rgb_uint8)
            
            # Save original size
            original_size = init_image.size
            init_image_resized = init_image.resize((512, 512))
            
            # Run inference
            result = self.pipe(
                prompt=prompt, 
                image=init_image_resized, 
                num_inference_steps=2,
                strength=strength, 
                guidance_scale=0.0
            ).images[0]
            
            # Resize result back to original size for the renderer
            result_restored = result.resize(original_size)
            result_srgb = np.array(result_restored).astype(np.float32) / 255.0
            
            # 3. Convert back from sRGB to Linear HDR so usdview can display it correctly
            result_linear_mapped = np.where(result_srgb <= 0.04045,
                                    result_srgb / 12.92,
                                    np.power((result_srgb + 0.055) / 1.055, 2.4))
            
            # Inverse Reinhard to restore HDR highlights
            result_linear = result_linear_mapped / np.clip(1.0 - result_linear_mapped, 0.001, 1.0)
            
            # Put back into RGBA buffer
            out_buffer = np.ones((height, width, 4), dtype=np.float32)
            out_buffer[:, :, :3] = result_linear
            
            # 4. Flip Y back to USD's bottom-left origin
            out_buffer = np.flipud(out_buffer)
            
            # Write response
            with open(RESPONSE_FILE + ".tmp", "wb") as f:
                f.write(struct.pack("I", 0x87654321))
                f.write(out_buffer.tobytes())
                
            os.rename(RESPONSE_FILE + ".tmp", RESPONSE_FILE)
            print("Response saved.")
            
            # Add to UI
            self.add_debug_images(init_image, result, prompt, aovs)
            
        except Exception as e:
            print(f"Error processing request: {e}")
            if os.path.exists(REQUEST_FILE):
                os.remove(REQUEST_FILE)

    def add_debug_images(self, img_in, img_out, prompt, aovs):
        row_widget = QWidget()
        row_layout = QHBoxLayout(row_widget)
        
        # Resize to thumbnails respecting aspect ratio, max width 256
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
        
        lbl_out = QLabel()
        qimg_out = QImage(img_out_thumb.tobytes(), img_out_thumb.width, img_out_thumb.height, QImage.Format_RGB888)
        lbl_out.setPixmap(QPixmap.fromImage(qimg_out))
        
        row_layout.addWidget(QLabel("Input:"))
        row_layout.addWidget(lbl_in)
        
        for name, aov_data in aovs.items():
            if name == 'depth':
                # Normalize depth for display
                d = aov_data[:, :, 0]
                d = np.nan_to_num(d)
                d = np.clip(d / np.max(d) * 255.0, 0, 255).astype(np.uint8)
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

        row_layout.addWidget(QLabel("Output:"))
        row_layout.addWidget(lbl_out)
        
        lbl_prompt = QLabel(f"Prompt: {prompt}")
        lbl_prompt.setWordWrap(True)
        lbl_prompt.setMaximumWidth(150)
        row_layout.addWidget(lbl_prompt)
        
        self.grid_layout.addWidget(row_widget)
        
        # Scroll to bottom
        QTimer.singleShot(100, lambda: self.scroll_area.verticalScrollBar().setValue(self.scroll_area.verticalScrollBar().maximum()))

if __name__ == "__main__":
    app = QApplication(sys.argv)
    
    print("Initializing AI Model (SD-Turbo)...")
    device = "cuda" if torch.cuda.is_available() else "cpu"
    dtype = torch.float16 if device == "cuda" else torch.float32
    variant = "fp16" if device == "cuda" else None

    try:
        pipe = AutoPipelineForImage2Image.from_pretrained(
            "stabilityai/sdxl-turbo", 
            torch_dtype=dtype,
            variant=variant
        )
        pipe.to(device)
        print(f"Model loaded successfully and running on {device.upper()}!")
    except Exception as e:
        print(f"Failed to load model: {e}")
        sys.exit(1)
        
    window = DebugServerWindow(pipe)
    window.show()
    
    sys.exit(app.exec())
