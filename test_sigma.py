import math
import random

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

def gaussian(x, center, sigma):
    t = (x - center) / sigma
    return math.exp(-0.5 * t * t)

intY = sum(yFit(l) for l in range(360, 831))
intX = sum(xFit(l) for l in range(360, 831))
intZ = sum(zFit(l) for l in range(360, 831))

X_E = intX / intY
Y_E = 1.0
Z_E = intZ / intY

r_E = 3.2404542 * X_E - 1.5371385 * Y_E - 0.4985314 * Z_E
g_E = -0.9692660 * X_E + 1.8760108 * Y_E + 0.0415560 * Z_E
b_E = 0.0556434 * X_E - 0.2040259 * Y_E + 1.0572252 * Z_E

import numpy as np

def get_M(sigma):
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
        integrate_peak(615.0, sigma),
        integrate_peak(540.0, sigma),
        integrate_peak(460.0, sigma)
    ]).T
    M_inv = np.linalg.inv(M)
    return M_inv @ np.array([1, 0, 0]), M_inv @ np.array([0, 1, 0]), M_inv @ np.array([0, 0, 1])

sigma = 20.0
WR, WG, WB = get_M(sigma)
print(f"Sigma {sigma}: WR={WR}, WG={WG}, WB={WB}")

def rgb_to_spec(r, g, b, lambdas):
    white = min(r, g, b)
    rem = (r - white, g - white, b - white)
    spec = []
    for l in lambdas:
        val = white
        val += rem[0] * gaussian(l, 615.0, sigma) * WR[0]
        val += rem[1] * gaussian(l, 540.0, sigma) * WG[1]
        val += rem[2] * gaussian(l, 460.0, sigma) * WB[2]
        spec.append(max(0.0, val))
    return spec

def spec_to_rgb(spec, lambdas):
    X, Y, Z = 0, 0, 0
    weight = (830 - 360) / len(lambdas) / 106.856
    for i in range(len(lambdas)):
        X += spec[i] * xFit(lambdas[i])
        Y += spec[i] * yFit(lambdas[i])
        Z += spec[i] * zFit(lambdas[i])
    X *= weight
    Y *= weight
    Z *= weight
    r = 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z
    g = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z
    b = 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z
    r /= r_E
    g /= g_E
    b /= b_E
    return r, g, b

colors = [(0.5, 0.5, 0.5), (1, 0.8, 0.2), (0.2, 0.8, 1), (1,1,0), (0,1,1), (1,0,1)]
N = 5000

for c in colors:
    r_acc, g_acc, b_acc = 0, 0, 0
    for _ in range(N):
        u = random.random()
        lambdas = [360 + u * (830 - 360)]
        for i in range(1, 4):
            l = lambdas[0] + i * (830 - 360) / 4
            if l > 830: l -= (830 - 360)
            lambdas.append(l)
        
        rgb = spec_to_rgb(rgb_to_spec(c[0], c[1], c[2], lambdas), lambdas)
        r_acc += rgb[0]
        g_acc += rgb[1]
        b_acc += rgb[2]
    
    print(f"{c} -> {r_acc/N:.3f}, {g_acc/N:.3f}, {b_acc/N:.3f}")
