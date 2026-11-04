import numpy as np
from natural_cover_mix import mix_natural_vocal

rate = 1000
time = np.arange(3000) / rate
source = (.1 * np.sin(2 * np.pi * 100 * time))[:, None]
converted = -2 * source
pitch = np.full(301, 200.0)
pitch[100:200] = 0
mixed = mix_natural_vocal(source, converted, pitch, rate)
scaled = source * 2
assert np.isfinite(mixed).all() and mixed.shape == converted.shape
assert np.max(np.abs(mixed[200:800] - (.9 * converted + .1 * scaled)[200:800])) < 1e-8
assert np.max(np.abs(mixed[1200:1800] - (.25 * converted + .75 * scaled)[1200:1800])) < 1e-6
assert np.array_equal(mix_natural_vocal(np.zeros_like(source), converted, pitch, rate), converted)
stronger = mix_natural_vocal(source, converted, pitch, rate, source_fraction=.25)
assert np.max(np.abs(stronger[200:800] - (.75 * converted + .25 * scaled)[200:800])) < 1e-8
print('NATURAL_MIX_OK: sustained notes, breath transitions, silent source, finite output')
