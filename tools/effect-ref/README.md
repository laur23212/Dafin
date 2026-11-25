# effect-ref: compare Dafin's frames with a reference you exported

`compare_frames.py` compares pictures, folders of pictures or videos, two at a time. It knows
nothing about where a reference came from: you export it yourself and give both sides. Nothing
from any other editor is kept in this repository, and the tests make their own pictures.

```
python3 tools/effect-ref/compare_frames.py --ref reference.mp4 --test dafin.mp4 --out report/ --frames 0,10,20
python3 tools/effect-ref/compare_frames.py --ref refs/ --test mine/ --out report/ --psnr 35 --ssim 0.98
```

Output: `report.csv` (one row per frame), `summary.json`, `contact.jpg` (reference | test |
difference x8). With no threshold given nothing fails; with one, exit code 1 means a frame missed
it, is missing on one side, or has another size (unless `--fit`). Exit code 2: the inputs are wrong.

Reading a row: a large `bias_*` is a brightness or tint error; `shift_x`/`shift_y` is the test
picture moved against the reference (a different crop, scale or timing); `worst_tile_*` is where
to look first. Effects are not expected to match pixel for pixel, so no default threshold is
imposed: calibrate on references you trust.

Tests: `python3 -m unittest discover -s tools/effect-ref -p 'test_*.py'`
