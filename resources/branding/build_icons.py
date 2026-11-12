"""Rebuild FrogShell branding assets from source.png using Pillow and NumPy.

Run with the bundled Python; outputs stay in resources/branding and
test-results/0.1.39/branding-assets. No network or installed tools needed.
Use --icns-only to build macOS assets from the approved frogshell.png without
rewriting source.png, frogshell.png, frogshell.ico, or existing previews.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUT = ROOT / "test-results" / "0.1.39" / "branding-assets"
SOURCE = HERE / "source.png"
SIZES = [16, 24, 32, 48, 64, 128, 256]
OUT.mkdir(parents=True, exist_ok=True)


def build_icns(report):
    """Encode explicit Lanczos RGBA layers and verify every decoded layer."""
    protected = [HERE / name for name in ("source.png", "frogshell.png", "frogshell.ico")]
    before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
    with Image.open(HERE / "frogshell.png") as approved:
        assert approved.mode == "RGBA" and approved.size == (1024, 1024)
        master = approved.copy()
    pixel_sizes = [32, 64, 128, 256, 512, 1024]
    layers = {s: master.copy() if s == 1024 else master.resize((s, s), Image.Resampling.LANCZOS)
              for s in pixel_sizes}
    path = HERE / "frogshell.icns"
    master.save(path, format="ICNS", append_images=list(layers.values()))

    data = path.read_bytes()
    assert data[:4] == b"icns" and struct.unpack_from(">I", data, 4)[0] == len(data)
    types = []
    pos = 8
    while pos < len(data):
        kind, length = struct.unpack_from(">4sI", data, pos)
        assert length >= 8 and pos + length <= len(data)
        types.append(kind.decode("ascii"))
        pos += length
    assert pos == len(data)

    verified = []
    decoded_layers = {}
    with Image.open(path) as container:
        assert container.format == "ICNS"
        for representation in sorted(container.info["sizes"]):
            frame = container.icns.getimage(representation).convert("RGBA")
            s = frame.width
            assert frame.size == (s, s) and s in layers
            assert frame.tobytes() == layers[s].tobytes(), "ICNS changed RGBA pixels"
            a = np.asarray(frame.getchannel("A"))
            corners = [int(a[y, x]) for x, y in ((0, 0), (s - 1, 0), (0, s - 1), (s - 1, s - 1))]
            assert corners == [0, 0, 0, 0]
            assert int(a.min()) == 0 and int(a.max()) == 255
            checks = {}
            for name in ("belly", "eye_white"):
                x, y = report["checks"][name]["point"]
                point = (min(s - 1, round(x * s / 1024)), min(s - 1, round(y * s / 1024)))
                checks[name] = {"point": list(point), "rgba": list(frame.getpixel(point))}
                assert checks[name]["rgba"][3] == 255
            verified.append({
                "logical_size": list(representation[:2]), "scale": representation[2],
                "pixel_size": list(frame.size), "mode": frame.mode,
                "corner_alpha": corners, "checks": checks,
                "transparent_pixels": int(np.count_nonzero(a == 0)),
                "partially_transparent_pixels": int(np.count_nonzero((a > 0) & (a < 255))),
                "matches_lanczos_source_pixels": True,
            })
            decoded_layers[s] = frame
    assert sorted(decoded_layers) == pixel_sizes
    assert decoded_layers[1024].tobytes() == master.tobytes()

    # Preview the decoded ICNS itself on pure light/dark backgrounds.
    preview = Image.new("RGB", (1120, 620), "#e5e7eb")
    draw = ImageDraw.Draw(preview)
    for row, background in enumerate(("#ffffff", "#1c2430")):
        draw.rectangle((0, row * 310, 1119, row * 310 + 309), fill=background)
        for col, s in enumerate((1024, 512, 256, 128)):
            frame = decoded_layers[s]
            display = frame.resize((256, 256), Image.Resampling.LANCZOS) if s > 256 else frame
            x = col * 280 + (280 - display.width) // 2
            y = row * 310 + 30 + (256 - display.height) // 2
            preview.paste(display, (x, y), display)
            draw.text((col * 280 + 12, row * 310 + 8), f"ICNS {s} x {s}", fill="#888888")
    preview.save(OUT / "preview-icns-light-dark.png")

    after = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
    assert before == after, "Approved asset bytes changed"
    report["icns"] = {
        "file": path.name, "source": "frogshell.png", "source_sha256": before["frogshell.png"],
        "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
        "encoder": "Pillow ICNS with explicit Lanczos RGBA append_images; lossless embedded PNG layers",
        "container_types": types, "pixel_sizes": pixel_sizes, "layers": verified,
        "master_1024_pixels_identical": True, "protected_asset_sha256": after,
        "rebuild": "build_icons.py --icns-only",
        "preview": "test-results/0.1.39/branding-assets/preview-icns-light-dark.png",
    }
    (HERE / "source-metadata.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return report


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--icns-only", action="store_true", help="Build ICNS from the approved PNG; preserve PNG/ICO/source bytes")
args = parser.parse_args()
if args.icns_only:
    report = json.loads((HERE / "source-metadata.json").read_text(encoding="utf-8"))
    print(json.dumps(build_icns(report)["icns"], indent=2, ensure_ascii=False))
    raise SystemExit(0)

image = Image.open(SOURCE).convert("RGB")
rgb = np.asarray(image).astype(np.float32)
lo = rgb.min(axis=2)
hi = rgb.max(axis=2)

# A dark or saturated pixel forms a barrier. Flood only from the image edge;
# enclosed light regions (belly and eye highlights) belong to the frog.
barrier = ((lo < 170) | ((hi - lo) > 50)).astype(np.uint8) * 255
regions = Image.fromarray(barrier, "L").copy()  # writable for floodfill
ImageDraw.floodfill(regions, (0, 0), 128)
outside = np.asarray(regions) == 128
solid = ~outside

# Limit matting to the outside edge of the silhouette. The exterior checker
# averages about 245; the adjacent outline is near 22. Unmix that matte from
# partial pixels so a pale fringe does not follow the frog onto dark surfaces.
solid_im = Image.fromarray(solid.astype(np.uint8) * 255, "L")
dilated = np.asarray(solid_im.filter(ImageFilter.MaxFilter(5))) > 0
exterior_im = Image.fromarray(outside.astype(np.uint8) * 255, "L")
near_exterior = np.asarray(exterior_im.filter(ImageFilter.MaxFilter(5))) > 0
edge = dilated & near_exterior
luma = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
coverage = np.clip((245.0 - luma) / 223.0, 0, 1)
coverage[coverage < 0.07] = 0
alpha = solid.astype(np.float32)
alpha[edge] = coverage[edge]
partial = edge & (alpha > 0) & (alpha < 1)
clean = rgb.copy()
clean[partial] = np.clip((rgb[partial] - 245.0 * (1 - alpha[partial, None])) / alpha[partial, None], 0, 255)
clean[alpha == 0] = 0
rgba = np.dstack((clean, alpha * 255 + 0.5)).astype(np.uint8)
cutout = Image.fromarray(rgba, "RGBA")
bbox = cutout.getchannel("A").getbbox()
cutout = cutout.crop(bbox)

# 6% padding on each side of the longest dimension.
target = 901
scale = min(target / cutout.width, target / cutout.height)
cutout = cutout.resize((round(cutout.width * scale), round(cutout.height * scale)), Image.Resampling.LANCZOS)
icon = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
offset = ((1024 - cutout.width) // 2, (1024 - cutout.height) // 2)
icon.alpha_composite(cutout, offset)
icon.save(HERE / "frogshell.png", optimize=True)
icon.save(HERE / "frogshell.ico", format="ICO", sizes=[(s, s) for s in SIZES])

# Inspect the exact ICO frames against both light and dark backgrounds.
ico = Image.open(HERE / "frogshell.ico")
frame_sizes = sorted(ico.ico.sizes())
font = ImageFont.load_default()
preview = Image.new("RGB", (1100, 760), "#e5e7eb")
draw = ImageDraw.Draw(preview)
for column, bg in enumerate(("#ffffff", "#1c2430")):
    x = 25 + column * 550
    draw.rectangle((x, 45, x + 525, 405), fill=bg)
    large = icon.resize((330, 330), Image.Resampling.LANCZOS)
    preview.paste(large, (x + 98, 60), large)
    draw.text((x, 15), "LIGHT BACKGROUND" if column == 0 else "DARK BACKGROUND", font=font, fill="#222222")
    draw.rectangle((x, 445, x + 525, 725), fill=bg)
    for i, size in enumerate((16, 24, 32, 48, 64, 128, 256)):
        frame = ico.ico.getimage((size, size)).convert("RGBA")
        # Show the actual frame without artificial enlargement.
        px = x + (i % 4) * 128 + (128 - size if size <= 128 else 0) // 2
        py = 485 + (i // 4) * 115
        if size == 256:
            # The 256 frame is shown separately above; avoid crowding the row.
            continue
        preview.paste(frame, (px, py), frame)
        draw.text((x + (i % 4) * 128 + 45, py - 20), str(size), font=font, fill="#777777")
preview.save(OUT / "preview-light-dark.png")

# Small sizes enlarged with nearest-neighbour interpolation reveal pixel detail.
small = Image.new("RGB", (960, 450), "#e5e7eb")
draw = ImageDraw.Draw(small)
for row, bg in enumerate(("#ffffff", "#1c2430")):
    draw.rectangle((0, row * 225, 960, row * 225 + 224), fill=bg)
    for col, size in enumerate((16, 24, 32)):
        frame = ico.ico.getimage((size, size)).convert("RGBA")
        zoom = frame.resize((192, 192), Image.Resampling.NEAREST)
        small.paste(zoom, (col * 320 + 64, row * 225 + 25), zoom)
        draw.text((col * 320 + 10, row * 225 + 8), f"{size} x {size}", font=font, fill="#888888")
small.save(OUT / "preview-small-pixels.png")

def output_point(x, y):
    return (round((x - bbox[0]) * scale) + offset[0], round((y - bbox[1]) * scale) + offset[1])

checks = {"corner": (0, 0), "belly": output_point(600, 620), "eye_white": output_point(385, 282)}
report = {
    "product": "FrogShell",
    "source": "source.png (user-provided clipboard image, unmodified)",
    "original_clipboard_path": "C:/Users/JOKER/AppData/Local/Temp/codex-clipboard-dfc88260-6c81-419b-936f-db6381896632.png",
    "source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
    "source_size": list(image.size),
    "source_mode": "RGB",
    "png_size": list(icon.size),
    "png_mode": icon.mode,
    "icon_sizes": [list(s) for s in frame_sizes],
    "checks": {name: {"point": list(point), "rgba": list(icon.getpixel(point))} for name, point in checks.items()},
    "transparent_pixels": int(np.count_nonzero(np.asarray(icon.getchannel("A")) == 0)),
    "partially_transparent_pixels": int(np.count_nonzero((np.asarray(icon.getchannel("A")) > 0) & (np.asarray(icon.getchannel("A")) < 255))),
    "dependencies": "Pillow and NumPy; bundled Codex Python; no installation required",
}
assert report["checks"]["corner"]["rgba"][3] == 0
assert report["checks"]["belly"]["rgba"][3] == 255
assert report["checks"]["eye_white"]["rgba"][3] == 255
assert frame_sizes == [(s, s) for s in SIZES]
report = build_icns(report)
print(json.dumps(report, indent=2, ensure_ascii=False))
