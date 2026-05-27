import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
from ocean_waves_fft import generate_ocean_cascades

# Generate waves
grid_size = 128
length = 200.0

cascades_config = [
    {
        "amplitude": 1e-4, 
        "wind_vel": (15.0, 5.0),
        "choppiness": 1.2,
        "min_k": 0.0,
        "max_k": 0.5
    }
]

Dx, Dy, Dz = generate_ocean_cascades(
    grid_size=grid_size,
    length=length,
    time=0.0,
    cascades=cascades_config,
    seed=42
)

print(f"X disp: {Dx.min()} to {Dx.max()}")
print(f"Y disp: {Dy.min()} to {Dy.max()}")
print(f"Z disp: {Dz.min()} to {Dz.max()}")

# Plot
x = np.linspace(0, length, grid_size, endpoint=False)
y = np.linspace(0, length, grid_size, endpoint=False)
X, Y = np.meshgrid(x, y)

X_disp = X + Dx
Y_disp = Y + Dy
Z_disp = Dz

fig = plt.figure(figsize=(10, 8))
ax = fig.add_subplot(111, projection='3d')
# plot only a subset for clarity if it's too dense
step = 2
ax.plot_surface(X_disp[::step, ::step], Y_disp[::step, ::step], Z_disp[::step, ::step], cmap='ocean', linewidth=0.1, antialiased=True, edgecolor='k')

# Set aspect ratio to be equal
ax.set_box_aspect((1, 1, 0.2))

plt.savefig('test_plot2.png')
print("Plot saved to test_plot2.png")
