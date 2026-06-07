# HdGemini Render Settings

HdGemini provides a wide array of render settings to control quality, performance, and specific visual features. You can adjust these settings in USDView under the `Renderer > Render Settings` menu or via USD APIs.

## General
- **Target Sample Count**: The number of path tracing samples per pixel. Higher values yield cleaner images but take longer to render. (Default: 32)
- **Max Reflection Bounces**: The maximum number of consecutive specular reflection bounces. (Default: 8)
- **Max Refraction Bounces**: The maximum number of consecutive transmission/refraction bounces through glass or water. (Default: 8)
- **Resolution Level**: Controls the viewport resolution scaling. 1 is full resolution, 2 is half, etc. (Default: 2)
- **Anti-Aliasing Filter**: The filter used for subpixel sampling (0=None, 1=Box, 2=Tent, 3=Gaussian). (Default: 1)
- **System Meters Per Unit**: Scales the scene geometry to physical units (e.g., 0.01 for centimeters). (Default: 0.01)
- **Enable SYCL GPU Acceleration**: Uses Intel SYCL to execute path tracing on compatible GPUs. (Default: false)
- **Enable On-Screen Stats**: Displays render statistics like sample count, FPS, and resolution on the screen. (Default: false)

## Adaptive Sampling
- **Enable Adaptive Sampling**: Stops path tracing on pixels that have already visually converged. (Default: true)
- **Adaptive Variance Threshold**: The noise threshold below which a pixel is considered converged. (Default: 0.01)
- **Adaptive Min Samples**: The minimum number of samples a pixel receives before adaptive sampling checks for convergence. (Default: 16)

## Features
- **Enable Subsurface Scattering**: Computes volumetric scattering for materials with subsurface properties (e.g., skin, wax). (Default: true)
- **Enable Subdivision**: Enables dynamic subdivision of meshes based on USD subdivision schemes. (Default: true)
- **Render Light Geometry**: Whether to render the emissive meshes and light objects as visible geometry. (Default: true)
- **Enable Path Guiding**: Uses learned irradiance fields to guide paths towards difficult light sources. (Default: true)

## Denoising and Post-Processing
- **Enable OIDN Denoiser**: Applies Intel Open Image Denoise (OIDN) to the rendered image. Automatically adjusts for adaptive sampling. (Default: true)
- **Enable Pre-Pass: Firefly Filter**: A heuristic filter to remove extreme outliers before the main denoiser. (Default: true)
- **Enable Pre-Pass: Chromaticity Blur**: A filter that softens chromatic noise. (Default: true)

## ReSTIR (Reservoir Spatio-Temporal Importance Resampling)
- **Enable ReSTIR Direct Illumination**: Uses ReSTIR for extremely fast and unbiased sampling of many lights. (Default: true)
- **Performance:Enable ReSTIR GI**: Uses ReSTIR to cache and resample indirect light paths, drastically improving interior and multi-bounce lighting. (Default: true)

## Physical Camera and Optics
- **Override Physical Camera Parameters**: Overrides USD camera attributes with the physical camera settings below. (Default: false)
- **Enable DoF**: Enables Depth of Field. Requires physical camera or USD camera focus attributes. (Default: false)
- **Focal Length (mm)**: The lens focal length. (Default: 50.0)
- **F-Stop (Aperture)**: The f-stop value. Lower values create a shallower depth of field. (Default: 5.6)
- **Focus Distance**: The distance from the camera to the focal plane. (Default: 10.0)
- **Bokeh Blades**: Number of blades in the camera aperture. 0 for perfectly circular bokeh. (Default: 0)
- **ISO**: Film/Sensor sensitivity. Higher values yield a brighter image. (Default: 100.0)
- **Shutter Speed**: Exposure time in seconds. (Default: 0.02)
- **Enable Lens Flare**: Enables a post-processing bloom/flare effect. (Default: false)
- **Lens Distortion**: Applies barrel or pincushion distortion. (Default: 0.0)
- **Chromatic Aberration**: Simulates color fringing towards the edges of the lens. (Default: 0.0)

## Physical Sky and Environment
- **Render IBL Background**: Whether to render the infinite dome light background directly to the camera. (Default: true)
- **Enable Physical Sky**: Replaces the dome light with a procedural physical sky model. (Default: false)
- **Physical Sky Azimuth**: The compass direction of the sun (0-360). (Default: 0.0)
- **Physical Sky Altitude**: The height of the sun above the horizon (0-90). (Default: 90.0)
- **Physical Sky Sun Exposure**: Exposure multiplier for the sun disk. (Default: 0.0)
- **Physical Sky Sky Exposure**: Exposure multiplier for the sky dome. (Default: 0.0)

## Volume Rendering
- **Volume Step Size**: Ray marching step size for heterogenous volumes. Smaller is more accurate but slower. (Default: 0.1)
- **Volume Density Scale**: Global multiplier for volume density. (Default: 1.0)

## Generative AI (Stable Diffusion Integration)
- **Generative AI: Enable**: Feeds the rendered image to a Stable Diffusion backend for image-to-image AI stylization. (Default: false)
- **Generative AI: Prompt**: The text prompt describing the desired style or content.
- **Generative AI: Strength**: How much the AI modifies the original image (0.0 = original, 1.0 = completely AI-generated). (Default: 0.5)

## Ocean Simulation
- **Ocean Enable**: Enables the procedural FFT ocean grid geometry. (Default: false)
- **Ocean Dicing Scale**: Tessellation multiplier for the ocean grid. (Default: 10.0)
- **Ocean Continuous Dicing**: Dynamically updates the tessellation density based on camera distance. (Default: false)
- **Ocean Water Height**: The world-space Y height of the ocean plane. (Default: 0.0)
- **Ocean Grid Size**: The FFT resolution (e.g., 128, 256, 512). (Default: 128)
- **Ocean Time**: Time parameter for wave animation. Can be driven by USD Time. (Default: 0.0)
- **Ocean Size**: The physical size of the simulated FFT patch in world units. (Default: 100.0)
- **Ocean Repeat**: Tiles the ocean patch infinitely. (Default: true)
- **Ocean Disable Shader**: Renders the ocean geometry with a default material instead of the complex water shader. (Default: false)
- **Ocean Scattering Color**: The base color of the deep water. (Default: 0.02, 0.15, 0.25)
- **Ocean Scattering Depth**: How deep the light penetrates the water before scattering back. (Default: 10.0)

### Ocean Waves Spectra (up to 3 cascaded layers)
- **Ocean Amplitude 1/2/3**: The height multiplier of each wave spectrum.
- **Ocean Choppiness 1/2/3**: Controls the sharpness of wave peaks. (Default: 1.2)
- **Ocean Wind Speed 1/2/3**: The wind speed driving the wave generation.
- **Ocean Wind Direction X/Y 1/2/3**: The vector direction of the wind.
- **Ocean Min K 1/2/3**: Minimum wavenumber (filters out large waves).
- **Ocean Max K 1/2/3**: Maximum wavenumber (filters out small waves).

### Camera Waterline Meniscus
Simulates the distortion and internal reflection when the camera lens touches the water surface.
- **Camera Waterline Meniscus Size**: (Default: 0.015)
- **Camera Waterline Meniscus Bend**: (Default: 0.2)
- **Camera Waterline Meniscus Tint**: (Default: 0.02, 0.05, 0.04)

## Debugging
- **Render Albedo Only (Debug)**: Bypasses path tracing and outputs the raw base color of the scene geometry. (Default: false)
