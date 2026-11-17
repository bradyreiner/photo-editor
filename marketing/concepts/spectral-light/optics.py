"""Procedural, wavelength-separated light passes for the Looksy reel.

Caustics use a thin refractive heightfield and forward photon-density splats.
This is an art-directed optics approximation, not a scientific color simulation.
No stock caustic footage or generated product interfaces are used.
"""
import numpy as np
from PIL import Image, ImageFilter


class Optics:
    def __init__(self, width=1080, height=1920, samples=900):
        self.w, self.h = width, height
        aspect = height / width
        x = np.linspace(-1.7, 1.7, samples, dtype=np.float32)
        y = np.linspace(-1.7 * aspect, 1.7 * aspect,
                        round(samples * aspect), dtype=np.float32)
        self.x, self.y = np.meshgrid(x, y)
        self.weight = np.exp(-0.22 * (self.x**2 + (self.y * 0.7)**2))
        self.screen_x, self.screen_y = np.meshgrid(
            np.linspace(-1, 1, width, dtype=np.float32),
            np.linspace(-aspect, aspect, height, dtype=np.float32))

    @staticmethod
    def display(linear):
        linear = np.clip(linear, 0, 1)
        return np.where(linear <= 0.0031308, 12.92 * linear,
                        1.055 * np.power(linear, 1 / 2.4) - 0.055)

    def caustic(self, t):
        x, y = self.x, self.y
        sx = np.zeros_like(x)
        sy = np.zeros_like(y)
        # Long, coherent waves produce folds rather than a tiled pool texture.
        for kx, ky, amp, rate, phase in [
            (2.6, 1.7, .092, .43, 0),
            (-1.8, 3.9, .041, -.31, 1.8),
            (5.1, -.8, .028, .52, 3.1),
            (1.1, 6.2, .019, -.27, 2.2),
        ]:
            wave = amp * np.cos(kx*x + ky*y + rate*t + phase)
            sx += kx * wave
            sy += ky * wave
        # A slow breathing lens deformation directs the light into the frame.
        radius = ((x - .30*np.sin(.3*t))**2 + (y + .2*np.cos(.2*t))**2)
        lens = .24 * np.exp(-radius / 1.7)
        sx -= x * lens
        sy -= y * lens
        channels = []
        aspect = self.h / self.w
        for dispersion in (.986, 1.0, 1.018):
            px = ((x + 2.25*dispersion*sx + 1.1) / 2.2 * self.w).astype(np.int32)
            py = ((y + 2.25*dispersion*sy + aspect*1.1) /
                  (aspect*2.2) * self.h).astype(np.int32)
            valid = (px >= 0) & (px < self.w) & (py >= 0) & (py < self.h)
            density = np.bincount((py[valid]*self.w + px[valid]).ravel(),
                                  weights=self.weight[valid].ravel(),
                                  minlength=self.w*self.h).reshape(self.h, self.w)
            # Stabilize the photon density, retaining concentrated bright folds.
            plate = Image.fromarray(np.clip(density * 29, 0, 255).astype(np.uint8))
            plate = plate.filter(ImageFilter.GaussianBlur(1.8))
            sharp = np.asarray(plate, dtype=np.float32) / 255
            radiance = np.maximum(sharp - .045, 0) ** 1.15 * 4.8
            channels.append(1 - np.exp(-radiance))
        linear = np.stack(channels, axis=2)
        glow = Image.fromarray((np.clip(linear, 0, 1)*255).astype(np.uint8))
        glow = np.asarray(glow.filter(ImageFilter.GaussianBlur(22)), dtype=np.float32)/255
        vignette = np.exp(-.55*(self.screen_x**2 + (self.screen_y*.45)**2))
        linear = (linear*.83 + glow*.24) * vignette[..., None]
        linear += np.array([.0018, .0021, .0030], dtype=np.float32)
        return Image.fromarray((self.display(linear)*255).astype(np.uint8))

    def additive(self, t):
        x, y = self.screen_x, self.screen_y
        linear = np.zeros((self.h, self.w, 3), dtype=np.float32)
        convergence = 1 - np.exp(-max(t, 0)*.8)
        for i, angle in enumerate((-.6, 1.5, 3.6)):
            spread = .72 * (1 - convergence) + .10
            cx = spread*np.cos(angle + t*.14)
            cy = spread*np.sin(angle + t*.14) + .30*np.sin(t*.17)
            # Elliptical volumetric pools mix additively in linear light.
            theta = angle + .22*np.sin(t*.3)
            u = (x-cx)*np.cos(theta) + (y-cy)*np.sin(theta)
            v = -(x-cx)*np.sin(theta) + (y-cy)*np.cos(theta)
            core = np.exp(-((u/.42)**2 + (v/1.4)**2)*2.0)
            haze = np.exp(-((u/.9)**2 + (v/1.8)**2)*2.0)*.11
            linear[..., i] = core*.94 + haze
        linear += .0018
        return Image.fromarray((self.display(1-np.exp(-linear*1.6))*255).astype(np.uint8))


if __name__ == '__main__':
    from pathlib import Path
    out = Path(__file__).resolve().parents[2]/'renders/spectral-light'
    out.mkdir(parents=True, exist_ok=True)
    optics = Optics()
    optics.caustic(1.7).save(out/'caustic-look.jpg', quality=96)
    optics.additive(1.7).save(out/'additive-look.jpg', quality=96)
