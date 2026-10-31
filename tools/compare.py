#!/usr/bin/env python3
"""Side-by-side compare: reference | ours | |diff| (amplified), plus a mean-error score.
usage: tools/compare.py ref.png ours.png out.png"""
import sys
from PIL import Image, ImageChops
import numpy as np
ref = Image.open(sys.argv[1]).convert("RGB")
ours = Image.open(sys.argv[2]).convert("RGB").resize(ref.size)
diff = ImageChops.difference(ref, ours)
d = np.asarray(diff).astype(np.float32)
score = d.mean()
amp = Image.fromarray(np.clip(d * 4, 0, 255).astype(np.uint8))
w, h = ref.size
out = Image.new("RGB", (w * 3 + 20, h), (255, 0, 255))
for i, im in enumerate((ref, ours, amp)):
    out.paste(im, (i * (w + 10), 0))
out.save(sys.argv[3])
print(f"{sys.argv[2]}: mean abs err {score:.2f}  (>{8} worth a look)")
