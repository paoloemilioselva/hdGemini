import numpy as np

def generate_ocean_cascades(grid_size, length, time, cascades, seed=None):
    """
    Generates ocean waves using FFT with multiple cascades to combine 
    long waves, shorter waves, and detailed crispy waves.
    
    Args:
        grid_size (int): Resolution of the grid (N x N).
        length (float): Physical length of the grid (meters).
        time (float): Current time in seconds.
        cascades (list of dict): List of dictionaries, each containing settings for a cascade:
            - 'amplitude': Global scaling factor for the spectrum (strength of the cascade)
            - 'wind_vel': (wx, wy) tuple for wind direction and speed
            - 'choppiness': Factor for horizontal displacement to make waves crispy
            - 'min_k': Minimum wave number (optional cutoff)
            - 'max_k': Maximum wave number (optional cutoff)
        seed (int): Optional random seed for reproducibility.
            
    Returns:
        Dx_total, Dy_total, Dz_total (np.ndarray): 2D arrays of combined displacements
                                                   along the x, y, and z axes.
    """
    N = grid_size
    Dx_total = np.zeros((N, N))
    Dy_total = np.zeros((N, N))
    Dz_total = np.zeros((N, N))
    
    # Base random state to allow repeatable cascades if seed is set
    rng = np.random.RandomState(seed) if seed is not None else np.random
    
    # Calculate wave numbers for the grid
    # fftfreq returns [0, 1/L, 2/L, ..., -1/L] * N in cycles per unit sample space
    # so we multiply by 2pi / (length / N) to get radians per meter
    k1d = 2 * np.pi * np.fft.fftfreq(N, d=length/N)
    kx, ky = np.meshgrid(k1d, k1d)
    
    k_sq = kx**2 + ky**2
    k_sq[0, 0] = 1e-8  # Avoid division by zero at origin
    k = np.sqrt(k_sq)
    
    g = 9.81  # Gravity (m/s^2)
    omega = np.sqrt(g * k)
    
    # Time evolution factors
    exp_i = np.exp(1j * omega * time)
    exp_minus_i = np.exp(-1j * omega * time)
    
    for cascade in cascades:
        amp = cascade.get('amplitude', 1.0)
        wind = cascade.get('wind_vel', (10.0, 0.0))
        chop = cascade.get('choppiness', 1.0)
        min_k = cascade.get('min_k', 0.0)
        max_k = cascade.get('max_k', np.inf)
        
        # Generate independent random numbers for this cascade
        xi_r = rng.normal(0, 1, (N, N))
        xi_i = rng.normal(0, 1, (N, N))
        
        # Calculate Phillips spectrum
        wind_dir = np.array(wind, dtype=float)
        wind_speed = np.linalg.norm(wind_dir)
        if wind_speed == 0:
            wind_speed = 0.001
            wind_dir = np.array([1.0, 0.0])
        else:
            wind_dir /= wind_speed
            
        L = (wind_speed ** 2) / g
        
        # Dot product between wave vector and wind direction
        k_dot_w = (kx * wind_dir[0] + ky * wind_dir[1]) / k
        k_dot_w[k_dot_w < 0] = 0  # Waves move mostly in the wind direction
        
        # Phillips spectrum formula
        ph = amp * np.exp(-1.0 / (k * L)**2) / (k**4) * (k_dot_w**2)
        ph[0, 0] = 0
        
        # Apply bandpass filter for the cascade
        ph[(k < min_k) | (k > max_k)] = 0
        
        # Scale to avoid grid resolution artifacts (optional scaling with dk)
        dk = (2 * np.pi / length) ** 2
        ph *= dk
        
        # Generate initial spectrum amplitudes
        h0 = (1.0 / np.sqrt(2.0)) * (xi_r + 1j * xi_i) * np.sqrt(ph)
        
        # Get h0(-k) which corresponds to reversed indices in discrete Fourier space
        idx_i = (N - np.arange(N)) % N
        idx_j = (N - np.arange(N)) % N
        h0_minus_k = h0[np.ix_(idx_i, idx_j)]
        
        # Frequency domain displacement
        h_k_t = h0 * exp_i + np.conj(h0_minus_k) * exp_minus_i
        
        kx_k = kx / k
        ky_k = ky / k
        kx_k[0, 0] = 0
        ky_k[0, 0] = 0
        
        # Displacements in frequency domain
        Dx_freq = -1j * kx_k * h_k_t
        Dy_freq = -1j * ky_k * h_k_t
        Dz_freq = h_k_t
        
        # Transform back to spatial domain
        # FFT normalizations can vary; standard ifft2 scales by 1/N^2.
        # But multiplying by N**2 keeps amplitude visually consistent.
        Dx = np.fft.ifft2(Dx_freq).real * chop * (N * N)
        Dy = np.fft.ifft2(Dy_freq).real * chop * (N * N)
        Dz = np.fft.ifft2(Dz_freq).real * (N * N)
        
        Dx_total += Dx
        Dy_total += Dy
        Dz_total += Dz
        
    return Dx_total, Dy_total, Dz_total

if __name__ == "__main__":
    # Example usage / Demo
    grid_size = 256
    physical_length = 200.0  # 200 meters
    current_time = 0.0
    
    # Define 3 cascades: long waves (swell), medium waves, and detailed crispy waves
    cascades_config = [
        {
            "name": "Long Waves",
            "amplitude": 1e-4, 
            "wind_vel": (15.0, 5.0),
            "choppiness": 1.2,
            "min_k": 0.0,
            "max_k": 0.5
        },
        {
            "name": "Medium Waves",
            "amplitude": 5e-5,
            "wind_vel": (10.0, 2.0),
            "choppiness": 1.5,
            "min_k": 0.5,
            "max_k": 2.0
        },
        {
            "name": "Crispy Details",
            "amplitude": 1e-5,
            "wind_vel": (5.0, 1.0),
            "choppiness": 2.0,
            "min_k": 2.0,
            "max_k": np.inf
        }
    ]
    
    Dx, Dy, Dz = generate_ocean_cascades(
        grid_size=grid_size,
        length=physical_length,
        time=current_time,
        cascades=cascades_config,
        seed=42
    )
    
    print(f"Generated ocean waves with {len(cascades_config)} cascades.")
    print(f"Displacement ranges:")
    print(f"X: {Dx.min():.2f} to {Dx.max():.2f}")
    print(f"Y: {Dy.min():.2f} to {Dy.max():.2f}")
    print(f"Z: {Dz.min():.2f} to {Dz.max():.2f}")
