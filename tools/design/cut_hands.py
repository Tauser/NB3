"""Cuts the user's hand drawings out of their reference board.

For each screen: white blobs are split into connected components; the ones
whose centre falls in the hand box are the hand. The hand sprite keeps the
hand's white, its interior lines (holes) and a thin black outline; the rest
is transparent. The eyes layer is the screen with the hand blacked out.
"""
import json
import sys
from collections import deque

import numpy as np
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
img = np.asarray(Image.open(src).convert("RGB")).astype(np.int32)
lum = (img[..., 0] * 299 + img[..., 1] * 587 + img[..., 2] * 114) // 1000

# name: (screen box x0, y0, x1, y1), (hand box x0, y0, x1, y1), absolute px
SCREENS = {
    "pensando": ((44, 169, 463, 458), (205, 355, 292, 440)),
    "confuso": ((513, 169, 933, 458), (552, 185, 695, 285)),
    "risadinha": ((984, 169, 1403, 458), (1135, 335, 1225, 435)),
    "sonolento": ((44, 586, 463, 876), (315, 722, 415, 810)),
    "combinado": ((513, 586, 933, 876), (800, 760, 885, 860)),
    "oi": ((984, 586, 1403, 876), (1250, 600, 1390, 710)),
}


def components(mask):
    h, w = mask.shape
    label = np.zeros((h, w), np.int32)
    comps = []
    for y in range(h):
        for x in range(w):
            if mask[y, x] and not label[y, x]:
                n = len(comps) + 1
                q = deque([(y, x)])
                label[y, x] = n
                pts = []
                while q:
                    cy, cx = q.popleft()
                    pts.append((cy, cx))
                    for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                        if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not label[ny, nx]:
                            label[ny, nx] = n
                            q.append((ny, nx))
                comps.append(np.array(pts))
    return label, comps


def fill_holes(mask):
    h, w = mask.shape
    outside = np.zeros_like(mask)
    q = deque()
    for y in range(h):
        for x in (0, w - 1):
            if not mask[y, x] and not outside[y, x]:
                outside[y, x] = True
                q.append((y, x))
    for x in range(w):
        for y in (0, h - 1):
            if not mask[y, x] and not outside[y, x]:
                outside[y, x] = True
                q.append((y, x))
    while q:
        cy, cx = q.popleft()
        for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
            if 0 <= ny < h and 0 <= nx < w and not mask[ny, nx] and not outside[ny, nx]:
                outside[ny, nx] = True
                q.append((ny, nx))
    return ~outside


def dilate(mask, r):
    out = mask.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dy * dy + dx * dx <= r * r:
                out |= np.roll(np.roll(mask, dy, 0), dx, 1)
    return out


meta = {}
for name, ((sx0, sy0, sx1, sy1), (hx0, hy0, hx1, hy1)) in SCREENS.items():
    s_lum = lum[sy0:sy1, sx0:sx1]
    white = s_lum > 140
    label, comps = components(white)
    hand = np.zeros_like(white)
    eyes_mask = np.zeros_like(white)
    sh, sw = white.shape
    for i, pts in enumerate(comps, start=1):
        cy, cx = pts.mean(axis=0)
        touches_edge = (pts[:, 0].min() <= 2 or pts[:, 1].min() <= 2 or
                        pts[:, 0].max() >= sh - 3 or pts[:, 1].max() >= sw - 3)
        if hy0 - sy0 <= cy <= hy1 - sy0 and hx0 - sx0 <= cx <= hx1 - sx0 and len(pts) > 8:
            hand |= label == i
        elif not touches_edge and len(pts) > 30:
            eyes_mask |= label == i
    body = fill_holes(dilate(hand, 1))
    alpha = dilate(body, 3)
    ys, xs = np.nonzero(alpha)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1

    rgba = np.zeros((y1 - y0, x1 - x0, 4), np.uint8)
    region = s_lum[y0:y1, x0:x1]
    inside = body[y0:y1, x0:x1]
    v = np.where(inside, region, 0).clip(0, 255).astype(np.uint8)
    rgba[..., 0] = rgba[..., 1] = rgba[..., 2] = v
    rgba[..., 3] = np.where(alpha[y0:y1, x0:x1], 255, 0)
    Image.fromarray(rgba, "RGBA").save(f"{out}/hand_{name}.png")

    # Eyes on transparent: the page draws the black screen behind them.
    eyes_rgba = np.zeros((sh, sw, 4), np.uint8)
    eyes_rgba[..., 0:3] = 255
    eyes_rgba[..., 3] = np.where(dilate(eyes_mask, 1), s_lum.clip(0, 255), 0).astype(np.uint8)
    Image.fromarray(eyes_rgba, "RGBA").save(f"{out}/eyes_{name}.png")
    meta[name] = {"screen": [int(sx1 - sx0), int(sy1 - sy0)],
                  "hand": [int(x0), int(y0), int(x1 - x0), int(y1 - y0)]}

print(json.dumps(meta, indent=1))
