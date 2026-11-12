# 更新托盘图标素材

`frogshell-update-source.png` 是用户提供的张嘴青蛙原图（1313×1198 RGB），背景棋盘是实像素。`frogshell-update.png` 是独立的 512×512 RGBA 运行资源，用于发现更新后的托盘状态；普通应用图标及其来源保持独立。

使用 Pillow 与 NumPy 运行 `python resources/branding/build_update_icon.py` 可从保留的原图重建。脚本沿用现有品牌处理的外部连通背景、边缘去底色方法，并仅保留与青蛙连通的主体以过滤棋盘压缩杂点；封闭的眼睛和肚子浅色区域保留。图像裁到完整轮廓后四周留出约 6% 透明边距。运行时仅生成 16 至 128 像素的托盘图标层，不常驻原始图像。
