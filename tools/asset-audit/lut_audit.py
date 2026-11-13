"""Read .cube LUTs the way src/engine/color/CubeLut.cpp expects and print size, range, clipping and grey cast.

usage: python3 -I lut_audit.py <folder with .cube files>
"""
import sys, glob, os, numpy as np
for f in sorted(glob.glob(os.path.join(sys.argv[1], '*.cube'))):
    size, dmin, dmax, rows, title = None, [0, 0, 0], [1, 1, 1], [], ''
    for line in open(f, encoding='utf-8'):
        t = line.split('#')[0].strip()
        if not t: continue
        k = t.split()
        if k[0] == 'TITLE': title = t[5:].strip().strip('"'); continue
        if k[0] == 'LUT_3D_SIZE': size = int(k[1]); continue
        if k[0] == 'DOMAIN_MIN': dmin = list(map(float, k[1:])); continue
        if k[0] == 'DOMAIN_MAX': dmax = list(map(float, k[1:])); continue
        rows.append(list(map(float, k)))
    a = np.array(rows)
    ok = size and len(a) == size ** 3 and a.shape[1] == 3
    lut = a.reshape(size, size, size, 3)  # [b][g][r] (red fastest)
    g = np.linspace(0, 1, size)
    ident = np.stack(np.meshgrid(g, g, g, indexing='ij')[::-1], -1)
    gray = np.array([lut[i, i, i] for i in range(size)])
    gray_cast = float(np.abs(gray - gray.mean(1, keepdims=True)).max())
    mid = size // 2
    dec = max(len(r.split('.')[-1]) for r in open(f).read().split() if r.replace('.', '').replace('-', '').isdigit() and '.' in r)
    print(f"{os.path.basename(f):28s} size={size} rows_ok={bool(ok)} domain={dmin}->{dmax} range=[{a.min():.3f},{a.max():.3f}] "
          f"clip%={100*float(((a<=0)|(a>=1)).mean()):.1f} mean_shift={float(np.abs(lut-ident).mean()):.3f} "
          f"gray_cast_max={gray_cast:.3f} mid_gray={np.round(lut[mid,mid,mid],3).tolist()} decimals={dec}")
