import numpy as np
from PIL import Image

RED_OFFSET = 300   # kırmızı noktadan sola kaç px
CROP_HALF  = 240   # 480x480 için yarı çap
TARGET_SIZE = 128  # final resize

def detect_red_dot(arr):
    r = arr[:,:,0].astype(int)
    g = arr[:,:,1].astype(int)
    b = arr[:,:,2].astype(int)
    mask = (r > 150) & (g < 80) & (b < 80)
    coords = np.where(mask)
    if len(coords[0]) == 0:
        return None, None
    return int(coords[1].mean()), int(coords[0].mean())  # x, y

def crop_and_resize(img_path):
    img = Image.open(img_path).convert("RGB")
    arr = np.array(img)
    w, h = img.size

    red_x, red_y = detect_red_dot(arr)

    if red_x is None:
        # Fallback: görüntü merkezini kullan
        cx, cy = w // 2, h // 2
    else:
        cx = red_x - RED_OFFSET
        cy = red_y

    left   = max(0, cx - CROP_HALF)
    top    = max(0, cy - CROP_HALF)
    right  = min(w, cx + CROP_HALF)
    bottom = min(h, cy + CROP_HALF)

    cropped = img.crop((left, top, right, bottom))
    resized = cropped.resize((TARGET_SIZE, TARGET_SIZE), Image.LANCZOS)
    return resized


img = crop_and_resize("t0_p0_c0_0010.jpg")
img_array = np.array(img) / 255.0  # normalize