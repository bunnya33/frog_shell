# Third-party components

| Component | Version used in this Windows build | License / source |
| --- | --- | --- |
| Qt Core, Gui, Widgets, Network | 6.8.3, dynamic libraries | LGPL-3.0 / GPL / commercial; https://www.qt.io/licensing/ |
| libssh2 | 1.11.1 | BSD-3-Clause; https://www.libssh2.org/ |
| libvterm | 0.3.3 | MIT; https://www.leonerd.org.uk/code/libvterm/ |
| libyaml | 0.2.5, static | MIT; https://github.com/yaml/libyaml; `licenses/libyaml.txt` |
| toml++ | 3.4.0, header-only | MIT; https://github.com/marzer/tomlplusplus; `licenses/tomlplusplus.txt` |
| OpenSSL | Qt SDK development package 3.0.16 | Apache-2.0; https://www.openssl.org/ |
| Dracula colors | pinned reference in prototype/themes/reference | MIT |
| GitHub VS Code Theme colors | pinned reference in prototype/themes/reference | MIT |
| Seti UI file icon outlines and VS Code Seti mapping | jesseweed/seti-ui 2d6c5e68b4ded73c92dac291845ee44e1182d511; Microsoft vscode-theme-seti | MIT; copyright (c) 2014 Jesse Weed; `licenses/SETI-NOTICES.txt` |
| Cascadia Code NF font | User-provided original Version 2407.024; family Cascadia Code NF | SIL Open Font License 1.1; `licenses/CASCADIA-CODE-OFL.txt`; https://github.com/microsoft/cascadia-code/tree/v2407.24 |

The embedded, unmodified `CascadiaCodeNF.ttf` is © 2021 Microsoft Corporation. All Rights Reserved. The official v2407.24 license preserves the Microsoft copyright and Reserved Font Name Cascadia Code. The font's actual name-table metadata (including its embedded license description), file hash and source are retained in `docs/cascadia-code-source.json` in the package (`resources/fonts/source.json` in source). The supplied metadata does not identify a separate Nerd Fonts modifier; no patch provenance is inferred from its NF filename. The font is an application resource registered once per process, not an installed system font. Its license remains separate from the application's license and does not license documents created with the font.

Seti outlines and associations were imported from the user's installed Visual Studio Code built-in `theme-seti` extension (Microsoft, MIT), with filename/extension/pattern language contributions from its built-in extensions. `resources/seti/source.json` records input hashes, source packages and their declared MIT licenses; `theme-package.json` retains the theme's original metadata. Microsoft copyright and the MIT permission notice are preserved in `licenses/VSCODE-LICENSE.txt` (https://github.com/microsoft/vscode/blob/main/LICENSE.txt); Jesse Weed's original notice is in `licenses/SETI-NOTICES.txt`. Development-only `scripts/import-seti-icons.py` extracts the original font contours with fonttools 4.61.0; neither fonttools, the original font, nor VS Code is a runtime dependency. Colors follow Seti's dark/light associations except the pale light default file ink, adjusted to #57606a for legibility. Original glyph contours retain their aspect ratio.

License texts are in `licenses/`. Qt is linked dynamically so it can be replaced with a compatible build. This repository includes the application build instructions and dependency versions. Packaging uses the Qt deployment tool; publishing a release also requires reviewing its copied runtime/plugin licenses and using maintained security patch versions of all dependencies.

RabbitShell 0.1.16 applies a narrow MIT-covered local resize/pending-wrap patch to build-directory copies of libvterm 0.3.3 screen.c/state.c. Original copyright (c) 2008 Paul Evans <leonerd@leonerd.org.uk> and full MIT permission/disclaimer remain in licenses/libvterm.txt. CMake checks both original source hashes and every patch anchor; fetched sources are unchanged. Source patch: cmake/PatchLibvterm.cmake; packaged source/version/hash/change notice: docs/libvterm-0.3.3-patch.md.

Source archives used by CMake:

- https://www.libssh2.org/download/libssh2-1.11.1.tar.gz — SHA-256 `d9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7`
- https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz — SHA-256 `09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0`
- https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz
- https://github.com/openssl/openssl/releases/tag/openssl-3.0.16
- https://codeload.github.com/yaml/libyaml/tar.gz/refs/tags/0.2.5 — SHA-256 `fa240dbf262be053f3898006d502d514936c818e422afdcf33921c63bed9bf2e`
- https://codeload.github.com/marzer/tomlplusplus/tar.gz/refs/tags/v3.4.0 — SHA-256 `8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155`
