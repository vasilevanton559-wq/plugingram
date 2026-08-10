"""Generate a spoiler-noise style icon for the Phone Blur plugin."""
from PIL import Image, ImageDraw
import os
import random

W = H = 128
random.seed(42)
margin = 4
radius = 28

img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
bg = Image.new("RGBA", (W, H), (0, 0, 0, 0))
ImageDraw.Draw(bg).rounded_rectangle(
    [margin, margin, W - 1 - margin, H - 1 - margin],
    radius=radius,
    fill=(36, 40, 48, 255),
)
img = Image.alpha_composite(img, bg)

noise = Image.new("RGBA", (W, H), (0, 0, 0, 0))
nd = ImageDraw.Draw(noise)
for _ in range(900):
    x = random.uniform(margin + 6, W - margin - 6)
    y = random.uniform(margin + 6, H - margin - 6)
    cx, cy = W / 2, H / 2
    if abs(x - cx) > 48 and abs(y - cy) > 48:
        continue
    r = random.uniform(0.7, 2.4)
    a = int(random.uniform(70, 210))
    g = int(random.uniform(160, 230))
    nd.ellipse([x - r, y - r, x + r, y + r], fill=(g, g, min(255, g + 10), a))

noise_soft = noise.resize((W // 2, H // 2), Image.BILINEAR).resize((W, H), Image.BILINEAR)
img = Image.alpha_composite(img, noise_soft)

noise2 = Image.new("RGBA", (W, H), (0, 0, 0, 0))
nd2 = ImageDraw.Draw(noise2)
for _ in range(400):
    x = random.uniform(margin + 8, W - margin - 8)
    y = random.uniform(margin + 8, H - margin - 8)
    r = random.uniform(0.5, 1.6)
    a = int(random.uniform(90, 240))
    g = int(random.uniform(180, 245))
    nd2.ellipse([x - r, y - r, x + r, y + r], fill=(g, g, g, a))
img = Image.alpha_composite(img, noise2)

mask = Image.new("L", (W, H), 0)
ImageDraw.Draw(mask).rounded_rectangle(
    [margin, margin, W - 1 - margin, H - 1 - margin],
    radius=radius,
    fill=255,
)
out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
out.paste(img, mask=mask)

_tools = os.path.dirname(os.path.abspath(__file__))
_root = os.path.dirname(_tools)
if os.path.isdir(os.path.join(_root, "Telegram")) and os.path.isdir(
    os.path.join(_root, "plugins")
):
    path = os.path.join(_root, "plugins", "noise", "icon.png")
else:
    path = os.path.join(_root, "plugingram", "plugins", "noise", "icon.png")
os.makedirs(os.path.dirname(path), exist_ok=True)
out.save(path, "PNG")
print("wrote", path)
