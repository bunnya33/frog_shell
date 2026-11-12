"""Development-only import: python import-seti-icons.py <VSCode resources/app/extensions>.

Requires fonttools 4.61.0 only during import. Runtime uses Qt paths, not fonts.
"""
import hashlib
import json
from pathlib import Path
import sys

from fontTools.pens.basePen import BasePen
from fontTools.ttLib import TTFont


class OutlinePen(BasePen):
    def __init__(self, glyphs):
        super().__init__(glyphs)
        self.commands = []

    def _moveTo(self, p):
        self.commands.append(["M", *p])

    def _lineTo(self, p):
        self.commands.append(["L", *p])

    def _curveToOne(self, a, b, c):
        self.commands.append(["C", *a, *b, *c])

    def _qCurveToOne(self, a, b):
        self.commands.append(["Q", *a, *b])

    def _closePath(self):
        self.commands.append(["Z"])


root = Path(__file__).resolve().parents[1]
extensions = Path(sys.argv[1])
source = extensions / "theme-seti"
destination = root / "resources/seti"
destination.mkdir(parents=True, exist_ok=True)
theme_bytes = (source / "icons/vs-seti-icon-theme.json").read_bytes()
theme = json.loads(theme_bytes)
(destination / "theme-package.json").write_bytes((source / "package.json").read_bytes())
(destination / "vs-seti-icon-theme.json").write_bytes(theme_bytes)
font_path = source / "icons/seti.woff"
font = TTFont(font_path)
glyphs = font.getGlyphSet()
cmap = font.getBestCmap()
outlines = {}
for definition in theme["iconDefinitions"].values():
    character = definition["fontCharacter"]
    if character not in outlines:
        pen = OutlinePen(glyphs)
        glyphs[cmap[int(character[1:], 16)]].draw(pen)
        outlines[character] = pen.commands
(destination / "outlines.json").write_text(json.dumps(outlines, separators=(",", ":")), encoding="utf-8")
languages = []
packages = []
for path in sorted(extensions.glob("*/package.json")):
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    contributions = data.get("contributes", {}).get("languages", [])
    # Installed distributions may bundle separately licensed extensions (e.g. Copilot).
    # Import filename metadata only from MIT built-ins covered by the retained VS Code license.
    if data.get("license") != "MIT":
        continue
    selected = []
    for language in contributions:
        if language["id"] in theme["languageIds"]:
            selected.append({key: language[key] for key in
                             ("id", "extensions", "filenames", "filenamePatterns") if key in language})
    if selected:
        languages.extend(selected)
        packages.append({"path": str(path.relative_to(extensions)).replace("\\", "/"),
                         "license": data.get("license"),
                         "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
(destination / "languages.json").write_text(json.dumps(languages, indent=2), encoding="utf-8")
metadata = {
    "theme": "Seti (Visual Studio Code)", "extensionId": "vscode.vscode-theme-seti",
    "sourceVersion": theme["version"], "fontTools": "4.61.0",
    "themeSha256": hashlib.sha256(theme_bytes).hexdigest(),
    "woffSha256": hashlib.sha256(font_path.read_bytes()).hexdigest(),
    "glyphs": len(outlines), "definitions": len(theme["iconDefinitions"]), "languagePackages": packages,
    "conversion": "Unhinted original font-unit contours; Y flipped and aspect-preserving centered at runtime."
}
(destination / "source.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
(root / "third_party/licenses/SETI-NOTICES.txt").write_bytes((source / "ThirdPartyNotices.txt").read_bytes())
print(json.dumps({"glyphs": len(outlines), "languages": len(languages), "packages": len(packages)}))
