# Scintilla 5.5.7 Qt font adaptation

FrogShell uses the official Scintilla Qt interface, not QScintilla. The original Scintilla permission and warranty disclaimer are retained in `third_party/licenses/Scintilla.txt`. The original Qt interface source headers retain their Scientific Toolworks and Archaeopteryx copyright notices.

`cmake/Scintilla.cmake` checks the original `qt/ScintillaEditBase/PlatQt.cpp` SHA-256 (`c283ba7f9b01cc3abda0ea2b9f98cf5f2394f09010d914f2e7e593decdb0e1ce`) and creates a patched copy only in the build directory. Fetched sources are unchanged.

The upstream `setBold(weight > 500)` discards intermediate Qt 6 numeric weights. The patch passes the numeric weight and the variable-font `wght` axis to QFont, preserving FrogShell's default Medium 500 and configured weight. It also sets the full-hinting preference already used by the application's content font. Source upgrades require reviewing the pinned hash and patch anchor.

Scintilla's Qt interface uses Qt Core5Compat for QTextCodec. Deploy `Qt6Core5Compat` with the other Qt shared libraries; the Windows release workflow installs `qt5compat`, and `windeployqt` discovers the linked module. Qt license terms remain separate from Scintilla's permission license.

The verified build copy of `src/PositionCache.cxx` (original SHA-256 `bb071e3d7a3cb1059e67bcbfd9ac84dc8273e9672d081b29d99a0ef98af1e192`) admits strings shorter than 128 bytes instead of 30 bytes. `EditorSurface` reduces the position-cache slots from 1024 to 256 at the same time. This includes Scintilla's approximately 100-byte layout segments without caching full lines or growing the cache with document length. The maximum cached text/width payload stays near the original budget. Style, font and encoding keys, matching, eviction and invalidation remain upstream's implementation; character metrics and shaping are unchanged.
