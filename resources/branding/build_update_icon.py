"""Regenerate the tray update icon from the supplied RGB source (Pillow, NumPy)."""
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

here = Path(__file__).resolve().parent
source = Image.open(here / "frogshell-update-source.png").convert("RGB")
assert source.size == (1313, 1198)
rgb = np.asarray(source, dtype=np.float32)
low, high = rgb.min(axis=2), rgb.max(axis=2)
barrier = ((low < 170) | ((high - low) > 50)).astype(np.uint8) * 255
regions = Image.fromarray(barrier, "L").copy()
ImageDraw.floodfill(regions, (0, 0), 128)
solid = np.asarray(regions) != 128

# Keep the single frog component; compressed checkerboard specks can otherwise
# become false isolated foreground pixels. The seed is inside its pale belly.
component = Image.fromarray(solid.astype(np.uint8) * 255, "L").copy()
assert component.getpixel((600, 700)) == 255
ImageDraw.floodfill(component, (600, 700), 128)
solid = np.asarray(component) == 128
outside = ~solid

# Remove the near-white matte only at the silhouette's exterior boundary.
dilated = np.asarray(Image.fromarray(solid.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(5))) > 0
near_outside = np.asarray(Image.fromarray(outside.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(5))) > 0
edge = dilated & near_outside
luma = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
coverage = np.clip((245.0 - luma) / 223.0, 0, 1)
coverage[coverage < 0.07] = 0
alpha = solid.astype(np.float32)
alpha[edge] = coverage[edge]
partial = edge & (alpha > 0) & (alpha < 1)
clean = rgb.copy()
clean[partial] = np.clip((rgb[partial] - 245.0 * (1 - alpha[partial, None])) / alpha[partial, None], 0, 255)
clean[alpha == 0] = 0
cutout = Image.fromarray(np.dstack((clean, alpha * 255 + 0.5)).astype(np.uint8), "RGBA")
bbox = cutout.getchannel("A").getbbox()
assert bbox == (202, 167, 1082, 1053)
cutout = cutout.crop(bbox)
scale = min(450 / cutout.width, 450 / cutout.height)
cutout = cutout.resize((round(cutout.width * scale), round(cutout.height * scale)), Image.Resampling.LANCZOS)
icon = Image.new("RGBA", (512, 512))
icon.alpha_composite(cutout, ((512 - cutout.width) // 2, (512 - cutout.height) // 2))
icon.save(here / "frogshell-update.png", optimize=True)
