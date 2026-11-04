"""Keep a little source singing and protect unvoiced transitions in the C mix."""
import numpy as np
from scipy.ndimage import gaussian_filter1d, label, uniform_filter1d


def mix_natural_vocal(source, converted, pitch, rate, source_fraction=.10):
    source = source.mean(axis=1, keepdims=True).copy()
    assert len(source) == len(converted) and len(pitch) > 1
    frame_times = np.arange(len(pitch)) * .01
    positions = np.minimum(np.rint(frame_times * rate).astype(int), len(source) - 1)
    voiced = pitch > 50
    window = max(1, int(.04 * rate))
    rms = np.sqrt(np.maximum(0, uniform_filter1d(source[:, 0] ** 2, size=window)[positions]))
    active = rms > .003
    stable = voiced & active
    if not stable.any():
        return converted.copy()
    converted_power = uniform_filter1d(np.mean(converted ** 2, axis=1), size=window)[positions]
    source *= np.sqrt(np.mean(converted_power[stable]) / np.mean(rms[stable] ** 2))
    # shortcut: voicing approximates breaths and long vowels; it does not isolate a clean breath stem.
    breath = gaussian_filter1d((~voiced & active).astype(float), sigma=2.5)
    regions, count = label(stable)
    sizes = np.bincount(regions, minlength=count + 1)
    sustained = (regions > 0) & (sizes[regions] >= 35)
    sustained = gaussian_filter1d(sustained.astype(float), sigma=4)
    amount = np.maximum(.75 * breath, source_fraction * sustained)
    assert np.isfinite(amount).all() and amount.min() >= 0 and amount.max() <= .751
    amount = np.interp(np.arange(len(source)) / rate, frame_times, amount)
    return (1 - amount[:, None]) * converted + amount[:, None] * source
