"""Render a static illustration of the current experimental resource panel.

This is sample data, not a screenshot or evidence of in-game behavior.
"""
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path

W, H = 1440, 900
image = Image.new("RGB", (W, H), "#10151f")
draw = ImageDraw.Draw(image)
font_path = "C:/Windows/Fonts/msyh.ttc"
font_bold = "C:/Windows/Fonts/msyhbd.ttc"
def font(size, bold=False): return ImageFont.truetype(font_bold if bold else font_path, size)
def box(x, y, w, h, fill, radius=14, outline=None):
    draw.rounded_rectangle((x, y, x+w, y+h), radius=radius, fill=fill, outline=outline, width=1)
def text(x, y, value, size=19, color="#e7edf7", bold=False):
    draw.text((x, y), value, font=font(size, bold), fill=color)
def check(x, y, active=False, locked=False):
    box(x, y, 22, 22, "#3b4657" if locked else "#398cf8" if active else "#303b4c", 5)
    if active: draw.line((x+5,y+11,x+10,y+16,x+18,y+6), fill="white", width=3)

box(38, 28, 1364, 844, "#1a202c", 20, "#3a4557")
box(38, 28, 1364, 74, "#232b39", 20)
draw.rectangle((38, 81, 1402, 102), fill="#232b39")
text(72, 49, "GakuActors  ·  场景资源管理", 25, bold=True)
box(1107, 48, 251, 34, "#3a3540", 8)
text(1120, 52, "示意图 · 示例数据", 16, "#ffd6a0")
box(68, 122, 258, 720, "#202837")
text(91, 147, "资源管理", 20, "#98a7bd", True)
box(83, 193, 228, 58, "#2e3e59", 9)
text(103, 208, "场景管理", 21, "#c9e2ff", True)
text(103, 284, "人物管理", 21, "#b5c0d0")
text(103, 357, "道具管理", 21, "#b5c0d0")
draw.line((91, 427, 302, 427), fill="#3c485c", width=1)
text(91, 450, "使用范围", 17, "#93a1b5", True)
text(91, 485, "拍摄 / Live 场景", 17)
text(91, 516, "资源控制默认关闭", 16, "#96a4b7")
box(91, 751, 205, 52, "#353044", 8)
text(106, 763, "实验功能 · 待实测", 16, "#f0c483")

text(359, 126, "场景", 27, bold=True)
text(359, 166, "独立管理场景背景、纯色底色与安全节点", 17, "#9baac0")
box(359, 210, 995, 84, "#222d3c")
check(381, 239, True)
text(418, 235, "启用资源显隐（实验）", 19, bold=True)
text(741, 237, "动作插件未加载时可用", 16, "#a8b9cf")
box(359, 314, 476, 206, "#222b39")
text(381, 335, "摄影背景替换", 20, bold=True)
text(381, 375, "下次摄影背景", 16, "#9baac0")
box(381, 405, 427, 45, "#303b4b", 8)
text(396, 414, "中庭舞台（日中）                 v", 18)
text(381, 470, "选择后在下次载入时生效", 15, "#9baac0")
box(855, 314, 499, 206, "#222b39")
text(878, 335, "纯色背景", 20, bold=True)
check(879, 382, True)
text(917, 377, "相机纯色背景", 18)
box(879, 425, 98, 52, "#34cb83", 8)
text(996, 433, "背景颜色", 17)
text(878, 484, "配合场景节点显隐使用", 15, "#9baac0")
box(359, 540, 995, 274, "#222b39")
text(381, 557, "当前场景节点", 20, bold=True)
box(1168, 551, 159, 36, "#334967", 7)
text(1185, 558, "扫描场景资源", 15, "#c7e1ff")
rows=[("-  PhotographyScene", False, True), ("    -  背景与建筑", False, False),
      ("        地面", False, False), ("        天空", True, False),
      ("    +  Lighting&PostEffects", False, False), ("    +  Camera / 系统节点", False, True)]
for i, (label, hidden, locked) in enumerate(rows):
    y=602+i*31
    text(385, y, label+("  [锁定]" if locked else ""), 16, "#7c8999" if locked else "#d4dfec")
    check(1193, y+1, hidden, locked)
    text(1226, y, "隐藏", 15, "#7c8999" if locked else "#c1ccda")
out=Path(__file__).with_name("scene-resources-preview.png")
image.save(out)
print(out)
