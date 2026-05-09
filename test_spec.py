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
    return r, g, b

def gaussian(x, center, sigma):
    t = (x - center) / sigma
    return math.exp(-0.5 * t * t)

def rgb_to_spec(r, g, b, lambdas):
    white = min(r, g, b)
    rem = (r - white, g - white, b - white)
    spec = []
    for l in lambdas:
        val = white
        val += rem[0] * gaussian(l, 615.0, 40.0) * 2.5
        val += rem[1] * gaussian(l, 540.0, 40.0) * 2.5
        val += rem[2] * gaussian(l, 460.0, 40.0) * 2.5
        spec.append(val)
    return spec

for _ in range(3):
    u = random.random()
    lambdas = [360 + u * (830 - 360)]
    for i in range(1, 4):
        l = lambdas[0] + i * (830 - 360) / 4
        if l > 830: l -= (830 - 360)
        lambdas.append(l)
    
    spec = rgb_to_spec(1, 1, 1, lambdas)
    print("1,1,1 ->", spec_to_rgb(spec, lambdas))
    spec = rgb_to_spec(1, 0, 0, lambdas)
    print("1,0,0 ->", spec_to_rgb(spec, lambdas))
    spec = rgb_to_spec(0, 1, 0, lambdas)
    print("0,1,0 ->", spec_to_rgb(spec, lambdas))
    spec = rgb_to_spec(0, 0, 1, lambdas)
    print("0,0,1 ->", spec_to_rgb(spec, lambdas))
