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
                
            # Delete request file so we don't process it again
            os.remove(REQUEST_FILE)
            
            print(f"Processing prompt: '{prompt}', strength: {strength}")
            
            # 1. Flip Y (USD has origin at bottom-left, PIL expects top-left)
            buffer = np.flipud(buffer)
            
            # 2. Convert from Linear to sRGB space for the AI model
            # Stable diffusion is trained on sRGB images!
            linear_rgb = np.clip(buffer[:, :, :3], 0.0, 1.0)
            srgb = np.where(linear_rgb <= 0.0031308, 
                           12.92 * linear_rgb, 
                           1.055 * np.power(linear_rgb, 1.0 / 2.4) - 0.055)
            
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
            
            # 3. Convert back from sRGB to Linear so usdview can display it correctly
            result_linear = np.where(result_srgb <= 0.04045,
                                    result_srgb / 12.92,
                                    np.power((result_srgb + 0.055) / 1.055, 2.4))
            
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
            self.add_debug_images(init_image, result, prompt)
            
        except Exception as e:
            print(f"Error processing request: {e}")
            if os.path.exists(REQUEST_FILE):
                os.remove(REQUEST_FILE)

    def add_debug_images(self, img_in, img_out, prompt):
        row_widget = QWidget()
        row_layout = QHBoxLayout(row_widget)
        
        # Resize to 256x256 thumbnails for UI
        img_in_thumb = img_in.resize((256, 256), Image.LANCZOS)
        img_out_thumb = img_out.resize((256, 256), Image.LANCZOS)
        
        lbl_in = QLabel()
        qimg_in = QImage(img_in_thumb.tobytes(), 256, 256, QImage.Format_RGB888)
        lbl_in.setPixmap(QPixmap.fromImage(qimg_in))
        
        lbl_out = QLabel()
        qimg_out = QImage(img_out_thumb.tobytes(), 256, 256, QImage.Format_RGB888)
        lbl_out.setPixmap(QPixmap.fromImage(qimg_out))
        
        lbl_prompt = QLabel(f"Prompt: {prompt}")
        lbl_prompt.setWordWrap(True)
        lbl_prompt.setMaximumWidth(150)
        
        row_layout.addWidget(lbl_in)
        row_layout.addWidget(lbl_out)
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
            "stabilityai/sd-turbo", 
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
