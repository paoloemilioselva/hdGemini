import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
from ocean_waves_fft import generate_ocean_cascades

# Generate waves
grid_size = 64
length = 100.0

cascades_config = [
    {
        "amplitude": 1.0, 
        "wind_vel": (10.0, 5.0),
        "choppiness": 1.5,
        "min_k": 0.0,
        "max_k": 2.0
    }
]

Dx, Dy, Dz = generate_ocean_cascades(
    grid_size=grid_size,
    length=length,
    time=0.0,
    cascades=cascades_config,
    seed=42
)

# Plot
x = np.linspace(0, length, grid_size)
y = np.linspace(0, length, grid_size)
X, Y = np.meshgrid(x, y)

X_disp = X + Dx
Y_disp = Y + Dy
Z_disp = Dz

fig = plt.figure(figsize=(10, 8))
ax = fig.add_subplot(111, projection='3d')
ax.plot_surface(X_disp, Y_disp, Z_disp, cmap='ocean', linewidth=0, antialiased=False)

plt.savefig('test_plot.png')
print("Plot saved to test_plot.png")
