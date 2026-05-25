import numpy as np

# --- Camera settings ---
screenWidth = 1920
screenHeight = 1080
camPos = np.array([0.0, 10.0, 0.0])
fov = 60.0
aspect = screenWidth / screenHeight
near = 0.1
far = 1000.0

def normalize(v):
    norm = np.linalg.norm(v)
    if norm == 0: return v
    return v / norm

def perspective(fovy, aspect, zNear, zFar):
    f = 1.0 / np.tan(np.radians(fovy) / 2.0)
    m = np.zeros((4, 4))
    m[0, 0] = f / aspect
    m[1, 1] = f
    m[2, 2] = (zFar + zNear) / (zNear - zFar)
    m[2, 3] = -1.0
    m[3, 2] = (2.0 * zFar * zNear) / (zNear - zFar)
    return m

def lookAt(eye, center, up):
    f = normalize(center - eye)
    u = normalize(up)
    s = normalize(np.cross(f, u))
    u = np.cross(s, f)
    m = np.eye(4)
    m[0, 0], m[1, 0], m[2, 0] = s[0], s[1], s[2]
    m[0, 1], m[1, 1], m[2, 1] = u[0], u[1], u[2]
    m[0, 2], m[1, 2], m[2, 2] = -f[0], -f[1], -f[2]
    m[3, 0] = -np.dot(s, eye)
    m[3, 1] = -np.dot(u, eye)
    m[3, 2] = np.dot(f, eye)
    return m

view = lookAt(camPos, camPos + np.array([0, -0.5, -1]), np.array([0, 1, 0]))
proj = perspective(fov, aspect, near, far)
viewProj = view @ proj

# --- Ocean Settings ---
maxDepth = 12
dicingScale = 1.0
waterHeight = 0.0
macroSize = 100.0 # Bounding box size for subdivision

# FFT Cascade Settings
gridSize = 128
sizes = [100.0, 10.0, 1.0]
amplitudes = [2.0, 0.5, 0.1]
choppiness = [1.2, 1.2, 1.2]
strengths = [1.0, 1.0, 1.0]
windSpeed = 10.0
windDirection = np.array([1.0, 1.0])
time_val = 1.0

# --- FFT Ocean Implementation ---
def generate_h0(N, size, amp, windSpeed, windDirection):
    V2 = windSpeed * windSpeed
    L = V2 / 9.81
    l = L / 1000.0
    wDir = windDirection / np.linalg.norm(windDirection)
    
    h0 = np.zeros((N, N), dtype=np.complex64)
    h0_minus = np.zeros((N, N), dtype=np.complex64)
    
    if size <= 1e-5 or amp <= 1e-5:
        return h0, h0_minus
        
    for z in range(N):
        for x in range(N):
            nx = x - N / 2.0
            nz = z - N / 2.0
            kx = (2.0 * np.pi * nx) / size
            kz = (2.0 * np.pi * nz) / size
            k_length = np.sqrt(kx*kx + kz*kz)
            
            if k_length < 1e-6:
                continue
                
            k_dot_w = (kx * wDir[0] + kz * wDir[1]) / k_length
            k_dot_w2 = k_dot_w * k_dot_w
            phillips = amp * np.exp(-1.0 / (k_length * k_length * L * L)) / (k_length**4) * k_dot_w2
            if k_dot_w < 0.0:
                phillips *= 0.07
            phillips *= np.exp(-k_length * k_length * l * l)
            
            sqrt_phillips = np.sqrt(max(0, phillips)) / np.sqrt(2.0)
            
            xi1 = np.random.normal(0, 1)
            xi2 = np.random.normal(0, 1)
            h0[z, x] = complex(xi1, xi2) * sqrt_phillips
            
            xi1_m = np.random.normal(0, 1)
            xi2_m = np.random.normal(0, 1)
            h0_minus[z, x] = complex(xi1_m, xi2_m) * sqrt_phillips
            
    return h0, h0_minus

def update_fft(N, size, chop, h0, h0_minus, t):
    h_kt_dz = np.zeros((N, N), dtype=np.complex64)
    h_kt_dx = np.zeros((N, N), dtype=np.complex64)
    h_kt_dy = np.zeros((N, N), dtype=np.complex64)
    
    for z in range(N):
        for x in range(N):
            nx = x - N / 2.0
            nz = z - N / 2.0
            kx = (2.0 * np.pi * nx) / size
            kz = (2.0 * np.pi * nz) / size
            k_length = np.sqrt(kx*kx + kz*kz)
            omega = np.sqrt(9.81 * k_length)
            
            phase = omega * t
            exp_iwt = complex(np.cos(phase), np.sin(phase))
            exp_minus_iwt = complex(np.cos(-phase), np.sin(-phase))
            
            h_kt = h0[z, x] * exp_iwt + h0_minus[z, x] * exp_minus_iwt
            
            kx_norm = kx / k_length if k_length > 1e-6 else 0.0
            kz_norm = kz / k_length if k_length > 1e-6 else 0.0
            
            h_kt_dz[z, x] = h_kt
            h_kt_dx[z, x] = complex(0.0, -kx_norm) * h_kt
            h_kt_dy[z, x] = complex(0.0, -kz_norm) * h_kt
            
    # Forward FFT mathematically (equivalent to our C++ implementation with sign flips)
    disp_dz = np.fft.ifft2(h_kt_dz) * (N * N)
    disp_dx = np.fft.ifft2(h_kt_dx) * (N * N)
    disp_dy = np.fft.ifft2(h_kt_dy) * (N * N)
    
    displacement = np.zeros((N, N, 3))
    
    for z in range(N):
        for x in range(N):
            sign = 1.0 if ((x + z) % 2 == 0) else -1.0
            
            dx = disp_dx[z, x].real * sign * chop
            dy = disp_dz[z, x].real * sign
            dz = disp_dy[z, x].real * sign * chop
            
            displacement[z, x] = [dx, dy, dz]
            
    return displacement

def get_displaced_position(basePos, displacements, N, sizes, strengths):
    totalDisp = np.zeros(3)
    
    for c in range(3):
        size = sizes[c]
        if size <= 1e-5: continue
        
        u = basePos[0] / size + 0.5
        v = basePos[2] / size + 0.5
        
        u = u - np.floor(u)
        v = v - np.floor(v)
        
        x = u * N
        z = v * N
        
        x0 = int(x)
        z0 = int(z)
        x1 = (x0 + 1) % N
        z1 = (z0 + 1) % N
        
        fx = x - x0
        fz = z - z0
        
        d00 = displacements[c][z0, x0]
        d10 = displacements[c][z0, x1]
        d01 = displacements[c][z1, x0]
        d11 = displacements[c][z1, x1]
        
        d0 = d00 * (1.0 - fx) + d10 * fx
        d1 = d01 * (1.0 - fx) + d11 * fx
        
        totalDisp += (d0 * (1.0 - fz) + d1 * fz) * strengths[c]
        
    return basePos + totalDisp

print("Generating H0 maps for 3 cascades...")
h0_maps = []
h0_minus_maps = []
for i in range(3):
    h0, h0_m = generate_h0(gridSize, sizes[i], amplitudes[i], windSpeed, windDirection)
    h0_maps.append(h0)
    h0_minus_maps.append(h0_m)

print("Running Inverse FFT to compute displacement maps...")
displacement_maps = []
for i in range(3):
    disp = update_fft(gridSize, sizes[i], choppiness[i], h0_maps[i], h0_minus_maps[i], time_val)
    displacement_maps.append(disp)

# --- QuadTree Mesh Generation ---
class QuadNode:
    def __init__(self, minBound, maxBound, depth):
        self.minBound = minBound
        self.maxBound = maxBound
        self.depth = depth

outNodes = []

def project_pt(p, viewProj):
    p4 = np.array([p[0], p[1], p[2], 1.0])
    pProj = p4 @ viewProj
    w = pProj[3]
    absW = max(1e-5, abs(w))
    ndc = np.array([pProj[0] / absW, pProj[1] / absW])
    return ndc, w

def subdivide_quad(node, outNodes, camPos, viewProj, screenWidth, screenHeight, maxDepth, dicingScale, waterHeight):
    if node.depth >= maxDepth:
        outNodes.append(node)
        return

    p0 = np.array([node.minBound[0], waterHeight, node.minBound[1]])
    p1 = np.array([node.maxBound[0], waterHeight, node.minBound[1]])
    p2 = np.array([node.maxBound[0], waterHeight, node.maxBound[1]])
    p3 = np.array([node.minBound[0], waterHeight, node.maxBound[1]])

    ndc0, w0 = project_pt(p0, viewProj)
    ndc1, w1 = project_pt(p1, viewProj)
    ndc2, w2 = project_pt(p2, viewProj)
    ndc3, w3 = project_pt(p3, viewProj)

    insideFrustum = True
    if w0 < 0 and w1 < 0 and w2 < 0 and w3 < 0:
        insideFrustum = False
    elif ndc0[0] < -1.5 and ndc1[0] < -1.5 and ndc2[0] < -1.5 and ndc3[0] < -1.5:
        insideFrustum = False
    elif ndc0[0] > 1.5 and ndc1[0] > 1.5 and ndc2[0] > 1.5 and ndc3[0] > 1.5:
        insideFrustum = False
    elif ndc0[1] < -1.5 and ndc1[1] < -1.5 and ndc2[1] < -1.5 and ndc3[1] < -1.5:
        insideFrustum = False
    elif ndc0[1] > 1.5 and ndc1[1] > 1.5 and ndc2[1] > 1.5 and ndc3[1] > 1.5:
        insideFrustum = False

    subdivide = False
    size = node.maxBound[0] - node.minBound[0]
    center3D = np.array([(node.minBound[0] + node.maxBound[0]) * 0.5, waterHeight, (node.minBound[1] + node.maxBound[1]) * 0.5])
    dist = np.linalg.norm(center3D - camPos)

    if insideFrustum:
        if w0 < 0 or w1 < 0 or w2 < 0 or w3 < 0:
            if size > max(0.1, dist * 0.05):
                subdivide = True
        else:
            e0 = np.linalg.norm(ndc1 - ndc0)
            e1 = np.linalg.norm(ndc2 - ndc1)
            e2 = np.linalg.norm(ndc3 - ndc2)
            e3 = np.linalg.norm(ndc0 - ndc3)
            maxEdgeNdc = max([e0, e1, e2, e3])
            maxPixelSize = maxEdgeNdc * 0.5 * max(screenWidth, screenHeight)
            if maxPixelSize > dicingScale:
                subdivide = True
    else:
        if size > max(1.0, dist * 0.2):
            subdivide = True

    if subdivide:
        center = (node.minBound + node.maxBound) * 0.5
        tl = QuadNode(node.minBound, center, node.depth + 1)
        tr = QuadNode(np.array([center[0], node.minBound[1]]), np.array([node.maxBound[0], center[1]]), node.depth + 1)
        bl = QuadNode(np.array([node.minBound[0], center[1]]), np.array([center[0], node.maxBound[1]]), node.depth + 1)
        br = QuadNode(center, node.maxBound, node.depth + 1)
        
        subdivide_quad(tl, outNodes, camPos, viewProj, screenWidth, screenHeight, maxDepth, dicingScale, waterHeight)
        subdivide_quad(tr, outNodes, camPos, viewProj, screenWidth, screenHeight, maxDepth, dicingScale, waterHeight)
        subdivide_quad(bl, outNodes, camPos, viewProj, screenWidth, screenHeight, maxDepth, dicingScale, waterHeight)
        subdivide_quad(br, outNodes, camPos, viewProj, screenWidth, screenHeight, maxDepth, dicingScale, waterHeight)
    else:
        outNodes.append(node)

print("Subdividing QuadTree grid...")
root = QuadNode(np.array([-macroSize, -macroSize]), np.array([macroSize, macroSize]), 0)
subdivide_quad(root, outNodes, camPos, viewProj, screenWidth, screenHeight, maxDepth, dicingScale, waterHeight)

print("Displacing generated vertices...")
points = []
indices = []
polycount = []

for leaf in outNodes:
    # Get base positions
    p0 = np.array([leaf.minBound[0], waterHeight, leaf.minBound[1]])
    p1 = np.array([leaf.maxBound[0], waterHeight, leaf.minBound[1]])
    p2 = np.array([leaf.maxBound[0], waterHeight, leaf.maxBound[1]])
    p3 = np.array([leaf.minBound[0], waterHeight, leaf.maxBound[1]])
    
    # Displace positions using FFT cascades
    p0_disp = get_displaced_position(p0, displacement_maps, gridSize, sizes, strengths)
    p1_disp = get_displaced_position(p1, displacement_maps, gridSize, sizes, strengths)
    p2_disp = get_displaced_position(p2, displacement_maps, gridSize, sizes, strengths)
    p3_disp = get_displaced_position(p3, displacement_maps, gridSize, sizes, strengths)
    
    baseIdx = len(points)
    points.extend([p0_disp, p1_disp, p2_disp, p3_disp])
    
    # 2 Triangles per quad
    indices.extend([baseIdx, baseIdx + 2, baseIdx + 1])
    indices.extend([baseIdx, baseIdx + 3, baseIdx + 2])
    polycount.extend([3, 3])

points = np.array(points)
indices = np.array(indices)
polycount = np.array(polycount)

print(f"Generated {len(outNodes)} quads.")
print(f"Vertices shape: {points.shape}")
print(f"Indices shape: {indices.shape}")
print(f"Polycount shape: {polycount.shape}")

with open("ocean_debug_displaced.obj", "w") as f:
    for p in points:
        f.write(f"v {p[0]} {p[1]} {p[2]}\n")
    for i in range(0, len(indices), 3):
        f.write(f"f {indices[i]+1} {indices[i+1]+1} {indices[i+2]+1}\n")
        
print("Saved ocean_debug_displaced.obj")
