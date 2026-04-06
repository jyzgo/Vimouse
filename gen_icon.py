"""Generate Vimouse icon - keyboard-driven mouse cursor with V + HJKL keys."""
from PIL import Image, ImageDraw, ImageFont
import math, os

SIZE = 256
img = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)

# --- Background: rounded rectangle with dark gradient ---
margin = 8
r = 32

for y in range(margin, SIZE - margin):
    t = (y - margin) / (SIZE - 2 * margin)
    r_c = int(18 + t * 12)
    g_c = int(22 + t * 14)
    b_c = int(32 + t * 18)
    for x in range(margin, SIZE - margin):
        in_rect = True
        # Check rounded corners
        dx = dy = 0
        if x < margin + r and y < margin + r:
            dx, dy = x - (margin + r), y - (margin + r)
        elif x > SIZE - margin - r and y < margin + r:
            dx, dy = x - (SIZE - margin - r), y - (margin + r)
        elif x < margin + r and y > SIZE - margin - r:
            dx, dy = x - (margin + r), y - (SIZE - margin - r)
        elif x > SIZE - margin - r and y > SIZE - margin - r:
            dx, dy = x - (SIZE - margin - r), y - (SIZE - margin - r)
        if (dx != 0 or dy != 0) and dx * dx + dy * dy > r * r:
            in_rect = False
        if in_rect:
            img.putpixel((x, y), (r_c, g_c, b_c, 255))

draw = ImageDraw.Draw(img)

# --- Glow effect behind the cursor ---
glow_cx, glow_cy = 118, 130
glow_r = 90
for y in range(max(0, glow_cy - glow_r), min(SIZE, glow_cy + glow_r)):
    for x in range(max(0, glow_cx - glow_r), min(SIZE, glow_cx + glow_r)):
        dist = math.sqrt((x - glow_cx) ** 2 + (y - glow_cy) ** 2)
        if dist < glow_r:
            t = 1.0 - dist / glow_r
            t = t * t
            px = img.getpixel((x, y))
            if px[3] > 0:
                nr = min(255, px[0] + int(t * 15))
                ng = min(255, px[1] + int(t * 55))
                nb = min(255, px[2] + int(t * 45))
                img.putpixel((x, y), (nr, ng, nb, px[3]))

draw = ImageDraw.Draw(img)

# --- Mouse cursor arrow ---
cx_off, cy_off = 38, 28

arrow_points = [
    (48 + cx_off, 30 + cy_off),
    (48 + cx_off, 160 + cy_off),
    (78 + cx_off, 130 + cy_off),
    (108 + cx_off, 175 + cy_off),
    (125 + cx_off, 160 + cy_off),
    (95 + cx_off, 118 + cy_off),
    (130 + cx_off, 118 + cy_off),
]

# Shadow
shadow_offset = 3
shadow_points = [(x + shadow_offset, y + shadow_offset) for x, y in arrow_points]
draw.polygon(shadow_points, fill=(0, 0, 0, 80))

# Cursor body
draw.polygon(arrow_points, fill=(0, 230, 180, 255))

# Inner lighter layer
icx = sum(p[0] for p in arrow_points) / len(arrow_points)
icy = sum(p[1] for p in arrow_points) / len(arrow_points)

for scale, color in [(0.88, (0, 255, 200, 255)), (0.65, (50, 255, 220, 255))]:
    inner = [(icx + (px - icx) * scale, icy + (py - icy) * scale) for px, py in arrow_points]
    draw.polygon(inner, fill=color)

# Outline
draw.polygon(arrow_points, outline=(0, 255, 190, 255), width=2)

# --- "V" letter on upper-right ---
v_x, v_y = 130, 32
v_w, v_h = 88, 95
stroke = 16

v_left = [
    (v_x, v_y),
    (v_x + stroke + 4, v_y),
    (v_x + v_w // 2 + 2, v_y + v_h),
    (v_x + v_w // 2 - stroke + 2, v_y + v_h - 6),
]
v_right = [
    (v_x + v_w, v_y),
    (v_x + v_w - stroke - 4, v_y),
    (v_x + v_w // 2 - 2, v_y + v_h),
    (v_x + v_w // 2 + stroke - 2, v_y + v_h - 6),
]

for pts in [v_left, v_right]:
    shadow = [(x + 2, y + 2) for x, y in pts]
    draw.polygon(shadow, fill=(0, 0, 0, 60))

draw.polygon(v_left, fill=(0, 230, 160, 255))
draw.polygon(v_right, fill=(0, 230, 160, 255))
draw.polygon(v_left, outline=(100, 255, 220, 255), width=1)
draw.polygon(v_right, outline=(100, 255, 220, 255), width=1)

# --- HJKL keyboard keys at bottom ---
key_labels = ['H', 'J', 'K', 'L']
key_size = 28
key_gap = 6
total_w = len(key_labels) * key_size + (len(key_labels) - 1) * key_gap
keys_x0 = (SIZE - total_w) // 2
keys_y = SIZE - margin - key_size - 16

font_small = None
for fp in ["C:/Windows/Fonts/consolab.ttf", "C:/Windows/Fonts/consola.ttf",
           "C:/Windows/Fonts/arial.ttf"]:
    if os.path.exists(fp):
        try:
            font_small = ImageFont.truetype(fp, 16)
            break
        except Exception:
            pass
if font_small is None:
    font_small = ImageFont.load_default()

for i, label in enumerate(key_labels):
    kx = keys_x0 + i * (key_size + key_gap)
    ky = keys_y

    # 3D shadow
    draw.rounded_rectangle(
        [kx + 1, ky + 2, kx + key_size + 1, ky + key_size + 2],
        radius=5, fill=(0, 0, 0, 100),
    )
    # Key body
    draw.rounded_rectangle(
        [kx, ky, kx + key_size, ky + key_size],
        radius=5, fill=(35, 42, 55, 255), outline=(0, 200, 160, 200), width=1,
    )
    # Key top highlight
    draw.rounded_rectangle(
        [kx + 2, ky + 1, kx + key_size - 2, ky + key_size - 4],
        radius=4, fill=(45, 55, 70, 255),
    )
    # Letter
    bbox = font_small.getbbox(label)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    tx = kx + (key_size - tw) // 2
    ty = ky + (key_size - th) // 2 - 2
    draw.text((tx, ty), label, fill=(0, 240, 190, 255), font=font_small)

# --- Subtle scan lines for tech feel ---
for y in range(margin, SIZE - margin, 4):
    for x in range(margin, SIZE - margin):
        px = img.getpixel((x, y))
        if px[3] > 0:
            img.putpixel(
                (x, y),
                (int(px[0] * 0.96), int(px[1] * 0.96), int(px[2] * 0.96), px[3]),
            )

# --- Save ---
out_png = "D:/Projects2025/Vimouse/icon_new.png"
img.save(out_png, 'PNG')
print(f"Saved PNG: {out_png}")

out_ico = "D:/Projects2025/Vimouse/MouseController_new.ico"
sizes = [(16, 16), (32, 32), (48, 48), (256, 256)]
img.save(out_ico, format='ICO', sizes=sizes)
print(f"Saved ICO: {out_ico}")
print("Done!")
