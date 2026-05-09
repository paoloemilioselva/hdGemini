import math
import numpy as np

def xFit(wave):
    t1 = (wave - 442.0) * (0.0624 if wave < 442.0 else 0.0374)
    t2 = (wave - 599.8) * (0.0264 if wave < 599.8 else 0.0323)
    t3 = (wave - 501.1) * (0.0490 if wave < 501.1 else 0.0382)
    return 0.362 * math.exp(-0.5 * t1 * t1) + 1.056 * math.exp(-0.5 * t2 * t2) - 0.065 * math.exp(-0.5 * t3 * t3)

def yFit(wave):
    t1 = (wave - 568.8) * (0.0213 if wave < 568.8 else 0.0247)
    t2 = (wave - 530.9) * (0.0613 if wave < 530.9 else 0.0322)
    return 0.821 * math.exp(-0.5 * t1 * t1) + 0.286 * math.exp(-0.5 * t2 * t2)

def zFit(wave):
    t1 = (wave - 437.0) * (0.0845 if wave < 437.0 else 0.0278)
    t2 = (wave - 459.0) * (0.0385 if wave < 459.0 else 0.0725)
    return 1.217 * math.exp(-0.5 * t1 * t1) + 0.681 * math.exp(-0.5 * t2 * t2)

intX = sum(xFit(l) for l in range(360, 831))
intY = sum(yFit(l) for l in range(360, 831))
intZ = sum(zFit(l) for l in range(360, 831))

X_E = intX / intY
Y_E = 1.0
Z_E = intZ / intY

r_E = 3.2404542 * X_E - 1.5371385 * Y_E - 0.4985314 * Z_E
g_E = -0.9692660 * X_E + 1.8760108 * Y_E + 0.0415560 * Z_E
b_E = 0.0556434 * X_E - 0.2040259 * Y_E + 1.0572252 * Z_E

def gaussian(x, center, sigma):
    t = (x - center) / sigma
    return math.exp(-0.5 * t * t)

def integrate_peak(center, sigma):
    X, Y, Z = 0, 0, 0
    weight = 1.0 / intY
    for l in range(360, 831):
        v = gaussian(l, center, sigma)
        X += v * xFit(l)
        Y += v * yFit(l)
        Z += v * zFit(l)
    X *= weight
    Y *= weight
    Z *= weight
    r = 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z
    g = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z
    b = 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z
    r /= r_E
    g /= g_E
    b /= b_E
    return [r, g, b]

M = np.array([
    integrate_peak(615.0, 40.0),
    integrate_peak(540.0, 40.0),
    integrate_peak(460.0, 40.0)
]).T  # Matrix maps (R_weight, G_weight, B_weight) -> (R, G, B)

print("Peak Matrix M:")
print(M)

M_inv = np.linalg.inv(M)
print("Inverse Matrix M_inv:")
print(M_inv)

# To get R=1, we need weights M_inv * [1, 0, 0]^T
W_R = M_inv @ np.array([1, 0, 0])
W_G = M_inv @ np.array([0, 1, 0])
W_B = M_inv @ np.array([0, 0, 1])

print("W_R:", W_R)
print("W_G:", W_G)
print("W_B:", W_B)

