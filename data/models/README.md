# OCR models (PP-OCRv4, ncnn)

PaddleOCR's PP-OCRv4 Chinese text detector and recogniser, converted from the
ONNX exports that ship inside `rapidocr-onnxruntime` 1.4.4 to ncnn with pnnx.
Rebuild them with `tools/ocr_models.sh`; check them with
`tools/ocr_models.sh verify IMAGE...`. Licence: Apache-2.0 (PaddleOCR).

| file | what | bytes |
|---|---|---|
| `ppocrv4_det.param` / `.bin` | DB text detector (MobileNetV3 + FPN) | 24 252 / 4 685 856 |
| `ppocrv4_rec.param` / `.bin` | SVTR-LCNet CTC recogniser | 19 602 / 10 750 388 |
| `ppocrv4_keys.txt` | the recogniser's 6623 characters, one per line | 26 250 |

Weights are stored fp32 (`fp16=0`), so the desktop build matches onnxruntime
to float rounding. One exact change is made to the detector before conversion
so that it survives fp16 weights (see "fp16" below).

Source models (sha256), from the rapidocr-onnxruntime 1.4.4 wheel:

```
d2a7720d45a54257208b1e13e36a8479894cb74155a5efe29462512d42f49da9  ch_PP-OCRv4_det_infer.onnx
48fc40f24f6d2a207a2b1091d3437eb3cc3eb6b676dc3ef9c37384005483683b  ch_PP-OCRv4_rec_infer.onnx
```

Converted files (sha256; the conversion is deterministic, rerunning gives the
same bytes):

```
c39ba0b1195e8ad37cfc5bdd7acedcfc1d9cb901e7b8eb1c9c4f6e5a66c04eb3  ppocrv4_det.param
d279ce224be70337034812fb662b1d3c262c4ce679428f75c6133a28744ee28b  ppocrv4_det.bin
2ae30dfdc7ee482c6cbbc0419c16ee125e880e9eaf69053d61953694bb0fcf4c  ppocrv4_rec.param
f91ebdcf4d6b58c58a87704160e59d07ae56bc2d5021eae09d095c5a20bf7525  ppocrv4_rec.bin
a1c84d9bdb9ab29043c58896224d32941783eb821629618416dcb08f12886492  ppocrv4_keys.txt
```

Tools: pnnx 20260526, ncnn (python) 1.0.20260526, onnx 1.23.1,
onnxruntime 1.30.0, rapidocr-onnxruntime 1.4.4, numpy 2.5.3,
opencv-python 5.0.0.93.

## ncnn requirements

- Use ncnn **20260526 or newer** (same release as the converter). The graphs
  use these layers; a trimmed ncnn build (`WITH_LAYER_*=OFF`) must keep all of
  them:
  - det: BinaryOp, Concat, Convolution, ConvolutionDepthWise, Deconvolution,
    HardSigmoid, HardSwish, Input, Interp, Pooling, Reshape, Sigmoid, Split
  - rec: BinaryOp, Concat, Convolution, ConvolutionDepthWise, Gemm,
    HardSigmoid, HardSwish, Input, LayerNorm, MultiHeadAttention, Permute,
    Pooling, Reshape, Softmax, Split, Squeeze, Swish
- Blob names: input `in0`, output `out0`, in both models.
- Both models take any input size (det: H and W multiples of 32; rec: height
  48, any width). No need to reshape or reload between calls.

## Detector

Input `in0`: a 3 x H x W float Mat, channels **B, G, R**, values
`(p / 255 - 0.5) / 0.5`, i.e. `p / 127.5 - 1` (RapidOCR's det mean/std
0.5/0.5, not PaddleOCR's ImageNet mean/std). The pages are greyscale, so all
three channels are the grey value: `PIXEL_GRAY2BGR`.

Size: the long side is limited to 960 and both sides are rounded to the
nearest multiple of 32 (PaddleOCR `limit_type=max`, `limit_side_len=960`):

```
ratio = max(h, w) > 960 ? 960.0 / max(h, w) : 1.0
rh = int(h * ratio), rw = int(w * ratio)            // truncate
H = round(rh / 32.0) * 32, W = round(rw / 32.0) * 32  // round half to even
if (H == 0 || W == 0) reject
```

Python's `round` is round-half-to-even: in C++ use `std::nearbyint` (default
rounding mode), not `std::round`. Upscaling small inputs is not done in this
mode (RapidOCR's own default is `limit_type=min, 736`, which upscales so the
short side is at least 736; that is far too big for the device).

C++:

```cpp
ncnn::Mat in = ncnn::Mat::from_pixels_resize(grey, ncnn::Mat::PIXEL_GRAY2BGR,
                                             w, h, W, H);
const float mean[3] = {127.5f, 127.5f, 127.5f};
const float norm[3] = {1 / 127.5f, 1 / 127.5f, 1 / 127.5f};
in.substract_mean_normalize(mean, norm);
ncnn::Extractor ex = det.create_extractor();
ex.input("in0", in);
ncnn::Mat prob;                       // 1 x H x W, sigmoid probability
ex.extract("out0", prob);
```

The reference resize is OpenCV `cv2.resize` (`INTER_LINEAR`). ncnn's
`from_pixels_resize` is also bilinear but rounds differently; "Verification"
below shows that this makes no difference in practice.

Output `out0`: 1 x H x W probability map, same size as the input. RapidOCR's
DB post-processing (config of 1.4.4): binarise at `thresh 0.3`, dilate with a
2x2 kernel, find contours (at most 1000), drop boxes whose short side < 3 or
whose mean probability inside the box < `box_thresh 0.5`, expand each box by
`unclip_ratio 1.6` (area * ratio / perimeter, pyclipper offset), then scale
back by `w / W`, `h / H` to page pixels.

## Recogniser

Crop: the line box, straightened by a perspective warp (`INTER_CUBIC`,
border replicate). **If the crop's height / width >= 1.5 it is rotated 90
degrees counter-clockwise** (`np.rot90`): this is how vertical columns are
read, with the column's top at the left of the line image.

Input `in0`: a 3 x 48 x W float Mat, channels B, G, R, values
`(p / 255 - 0.5) / 0.5`, then padded on the right with **0.0** (which is
mid-grey, not white) up to W:

```
ratio = w / (float)h                         // crop size
W  = int(48 * max(320 / 48.0, ratio))        // at least 320
rw = min(int(ceil(48 * ratio)), W)           // resized width
resize crop to rw x 48 (bilinear), normalise, place at x = 0..rw-1,
fill x = rw..W-1 with 0.0 in all three channels
```

C++:

```cpp
ncnn::Mat line = ncnn::Mat::from_pixels_resize(crop, ncnn::Mat::PIXEL_GRAY2BGR,
                                               cw, ch, rw, 48);
line.substract_mean_normalize(mean, norm);   // same mean/norm as det
ncnn::Mat in(W, 48, 3);
in.fill(0.f);
for (int c = 0; c < 3; c++)
    for (int y = 0; y < 48; y++)
        memcpy(in.channel(c).row(y), line.channel(c).row(y), rw * sizeof(float));
ex.input("in0", in);
ncnn::Mat prob;                // T x 6625 softmax probabilities
ex.extract("out0", prob);      // prob.h == T, prob.w == 6625
```

Output `out0`: T x 6625 (h = T time steps, w = classes). The values are
already **softmax probabilities**, not logits. T = W / 8 when W is a
multiple of 8 (320 -> 40, 500 -> 62, 1001 -> 125; take it from `prob.h`).
Column t covers about input pixels `8t .. 8t+7` of the 48-high line image.
Map it back to the crop with `* w / rw`, and for vertical lines undo the
rot90: x in the rotated line is the distance from the column's top.

### Classes and keys

| class | meaning |
|---|---|
| 0 | CTC blank |
| 1 .. 6623 | `ppocrv4_keys.txt` line k (1-based), one UTF-8 character |
| 6624 | `' '` (ASCII space, RapidOCR's `use_space_char`) |

`ppocrv4_keys.txt` is exactly the ONNX metadata `character`, `\n`-separated,
with a final `\n`, 6623 lines, no duplicates. Line 5710 is U+3000 (ideographic
space): read lines without trimming whitespace. 6277 of the entries are CJK
ideographs; the rest are digits, Latin, punctuation and symbols, so the app
must filter candidates to hanzi itself.

Decoding (RapidOCR `CTCLabelDecode`, greedy): take the argmax of each column,
drop a column equal to the previous column's argmax, drop class 0, map the
rest through the table above. The line's score is the mean of the kept
columns' max probability; RapidOCR discards lines with score < 0.5
(`text_score`). Per-column top-k (for tap candidates) is just the k largest
entries of that column.

## fp16

The `.bin` files are fp32. ncnn's defaults on ARM (`opt.use_fp16_storage`,
`use_fp16_packed`; `use_fp16_arithmetic` on ARMv8.2) convert weights, and
store activations, as fp16 at load time. Two things follow:

- **The original detector does not survive fp16 weights.** The DB head's
  3x3 conv (96 -> 24) is followed by a BatchNorm whose running variance
  reaches 9.8e7. Folded together, 95% of that conv's weights are below fp16's
  smallest normal (6.1e-5; some are 8e-10), and with fp16 weights 15-18% of
  the 0.3-thresholded map flipped. `tools/ocr_models.sh` therefore patches
  the ONNX before conversion: BN.1's gamma and beta x 256, the following
  ConvTranspose's weights / 256. The chain is BN -> ReLU -> ConvTranspose and
  ReLU(256 z) = 256 ReLU(z), so the change is exact (powers of two; the fp32
  numbers below are against the *unpatched* ONNX). The largest activation in
  between is about 2.7 x 256 on real pages, far below fp16's 65504.
- With that patch, fp16 weights are close but not identical (numbers below:
  0.05% of mask pixels, 1 line in 167 lost its first character, the top-5
  order changed in 7% of columns). That test covered fp16 *weights* only (x86
  computes in fp32); fp16 activation storage on the device was not measured.

Recommendation for the C++ code: set `opt.use_fp16_storage`,
`use_fp16_packed` and `use_fp16_arithmetic` to `false` (and
`use_bf16_storage` false) on both nets. That is what the verification ran,
and it makes the device compute what was checked here. Turning fp16 on is a
speed optimisation to measure on the hardware later. `pnnx ... fp16=1` would
halve the files (2.4 + 5.4 MB); the script makes those in its work directory
for the comparison but does not ship them.

## Verification

`tools/ocr_models.sh verify IMAGE...` runs every image through both
models in ncnn (python bindings, the same release) and onnxruntime, with the
**same input tensors**, built by RapidOCR's own preprocessing code. The
onnxruntime side uses the original, unpatched ONNX. It exits non-zero unless
the fp32 ncnn models decode identically column by column.

Run of 2026-10-05 on 36 real inputs: 25 scanned 连环画 pages (5 each from
fsb15, fsb30, hlm16, hlm22 and sgyy60, 1500 px wide, with horizontal speech
balloons and vertical caption columns) and 11 caption strips (about
2600 x 320, three lines each). The detector ran twice per image, at long side
960 and at long side 1500 (72 runs). The recogniser ran on every line
RapidOCR found, cropped and rotated its way (167 lines, 15 425 CTC columns):

| check | fp32 ncnn (shipped) | fp16 weights (not shipped) |
|---|---|---|
| det probability, max abs diff | 1.2e-4 | 0.47 |
| det probability, mean abs diff | 1.8e-8 | 5.8e-5 |
| det mask (> 0.3) pixels that differ, worst image | 0 | 0.048% |
| rec probability, max abs diff | 1.4e-3 | 0.31 |
| rec columns whose argmax differs | 0 / 15 425 | 1 / 15 425 |
| rec columns whose top-5 order differs | 1 (a near tie) | 1 123 |
| decoded lines that differ | **0 / 167** | 1 / 167 |

The remaining fp32 differences come from ncnn's SIMD exp and its attention
accumulation order. They do not change a single argmax.

The C++ preprocessing (ncnn's `from_pixels_resize` and
`substract_mean_normalize` instead of OpenCV) was checked the same way. The
det input tensor differs from OpenCV's by at most 0.008 (about 1 grey level,
from bilinear rounding), and 0.001% of mask pixels differ on average (0.007%
worst). The rec text was identical on 167 / 167 lines.

Padding matters, so reproduce it. RapidOCR recognises 6 lines per batch, all
padded to the widest in the batch, so a line's text depends on its
neighbours: batch-of-6 differed from one-line-at-a-time on 18 / 167 lines.
The reference above is one line per batch, padded to at least 320. Leaving out
the padding entirely (input width = resized width, which ncnn accepts)
changed 8 / 167 lines, typically by gaining or losing a character at the
end. The C++ code should recognise one line at a time with the minimum-320
padding described above.

Peak memory of the verify run was 2.5 GB, and it took about 5 minutes with
8 threads. Do not feed it RapidOCR's default det size (short side >= 736): a
320-high strip becomes 736 x 5920, and an earlier run was OOM-killed.
