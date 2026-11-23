#!/bin/sh
# Rebuild data/models/ (the on-device OCR models) from PaddleOCR's PP-OCRv4
# ONNX exports, and check the ncnn conversion against onnxruntime.
#
#   tools/ocr_models.sh            convert: venv, pnnx, keys -> data/models/
#   tools/ocr_models.sh verify IMG...
#                                  run det + rec through ncnn and onnxruntime
#                                  on real pages (PNG/JPEG) and compare
#
# The source models are the ones shipped inside the rapidocr-onnxruntime 1.4.4
# wheel (ch_PP-OCRv4_det_infer.onnx, ch_PP-OCRv4_rec_infer.onnx; Apache-2.0,
# PaddleOCR), checked by sha256 below. Conversion is pnnx; the param/bin it
# writes need an ncnn at least as new as the pinned one (Gemm, LayerNorm and
# MultiHeadAttention layers). Preprocessing the C++ code must reproduce is in
# data/models/README.md.
#
# Everything big (venv ~540 MB, pnnx work files) goes under $HANZI_BUILD, never
# in the repo or in ~: the root disk is full. This script writes only:
#   $HANZI_BUILD/venv            (created once, reused)
#   $HANZI_BUILD/ocr_models/     (pnnx work files, overwritten)
#   data/models/ppocrv4_{det,rec}.{param,bin}, data/models/ppocrv4_keys.txt
set -e
cd "$(dirname "$0")/.."
REPO=$(pwd)

HANZI_BUILD=${HANZI_BUILD:-$PWD/.deps}
VENV=$HANZI_BUILD/venv
WORK=$HANZI_BUILD/ocr_models
OUT=$REPO/data/models

# Pinned tools. ncnn's python wheel and pnnx come from the same upstream
# release (20260526), so the converter and the checker agree on layer params.
PNNX_VER=20260526
NCNN_VER=1.0.20260526
ONNX_VER=1.23.1
ORT_VER=1.30.0
RAPIDOCR_VER=1.4.4
NUMPY_VER=2.5.3
OPENCV_VER=5.0.0.93

DET_SHA=d2a7720d45a54257208b1e13e36a8479894cb74155a5efe29462512d42f49da9
REC_SHA=48fc40f24f6d2a207a2b1091d3437eb3cc3eb6b676dc3ef9c37384005483683b

# pip's cache and temp files would otherwise land in ~ and /tmp.
export PIP_CACHE_DIR="$HANZI_BUILD/pipcache"
export TMPDIR="$HANZI_BUILD/tmp"
mkdir -p "$HANZI_BUILD" "$TMPDIR" "$WORK"

if [ ! -x "$VENV/bin/python" ]; then
    echo "venv in $VENV ..."
    python3 -m venv "$VENV"
fi
PY=$VENV/bin/python
if ! "$PY" -c "import ncnn, onnx, onnxruntime, rapidocr_onnxruntime" 2>/dev/null; then
    echo "pinned tools into $VENV ..."
    "$VENV/bin/pip" install -q --upgrade pip
    # pnnx's wheel declares torch for its python API; the converter itself
    # is the bundled static binary and needs nothing else, so skip the deps.
    "$VENV/bin/pip" install -q --no-deps "pnnx==$PNNX_VER"
    "$VENV/bin/pip" install -q "ncnn==$NCNN_VER" "onnx==$ONNX_VER" \
        "onnxruntime==$ORT_VER" "rapidocr-onnxruntime==$RAPIDOCR_VER" \
        "numpy==$NUMPY_VER" "opencv-python==$OPENCV_VER"
fi
# (importing pnnx would need torch; only locate its package directory)
PNNX=$("$PY" -c "import os, importlib.util as u; print(os.path.join(u.find_spec('pnnx').submodule_search_locations[0], 'pnnx'))")
chmod +x "$PNNX"
MODELS=$("$PY" -c "import os, rapidocr_onnxruntime as r; print(os.path.join(os.path.dirname(r.__file__), 'models'))")

if [ "${1:-convert}" = verify ]; then
    shift
    [ $# -gt 0 ] || { echo "usage: $0 verify IMAGE..." >&2; exit 2; }
    exec nice -n 10 "$PY" - "$OUT" "$MODELS" "$WORK" "$@" <<'EOF'
# ncnn vs onnxruntime on identical input tensors, plus the ncnn-native
# preprocessing (what the C++ code will do) against RapidOCR's own.
import sys, os, numpy as np, cv2, ncnn, onnxruntime as ort
from rapidocr_onnxruntime import RapidOCR
from rapidocr_onnxruntime.ch_ppocr_det.utils import DetPreProcess

out_dir, models, work, images = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
eng = RapidOCR()
# RapidOCR's default det size (short side >= 736) upscales a 320-high caption
# strip to 736 x 5920 and needs GBs; find its lines at long side <= 1500.
eng.text_det.limit_type = 'max'
eng.text_det.get_preprocess = lambda max_wh: DetPreProcess(1500, 'max', [.5] * 3, [.5] * 3)
decode = eng.text_rec.postprocess_op            # CTCLabelDecode, blank=0, ' ' last
keys = open(os.path.join(out_dir, 'ppocrv4_keys.txt'), encoding='utf-8').read().split('\n')[:-1]
assert decode.character == ['blank'] + keys + [' '], 'keys file != RapidOCR classes'

so = ort.SessionOptions(); so.intra_op_num_threads = 8
so.enable_cpu_mem_arena = False                 # shapes vary; keep memory flat
det_ort = ort.InferenceSession(os.path.join(models, 'ch_PP-OCRv4_det_infer.onnx'), so)
rec_ort = ort.InferenceSession(os.path.join(models, 'ch_PP-OCRv4_rec_infer.onnx'), so)

def net(path, fp16):
    n = ncnn.Net()
    n.opt.num_threads = 8
    n.opt.use_vulkan_compute = False
    n.opt.use_fp16_packed = n.opt.use_fp16_storage = n.opt.use_fp16_arithmetic = fp16
    n.opt.use_bf16_storage = False
    assert n.load_param(path + '.param') == 0
    assert n.load_model(path + '.bin') == 0
    return n
# fp32: the committed models, fp32 compute -- must match onnxruntime.
# fp16: the same graph with fp16 weights (made by convert, kept in $WORK) and
# fp16 storage on, roughly what ncnn does by default on ARM; reported only.
nets = {'fp32': (net(os.path.join(out_dir, 'ppocrv4_det'), False),
                 net(os.path.join(out_dir, 'ppocrv4_rec'), False))}
if os.path.exists(os.path.join(work, 'det16.ncnn.bin')):
    nets['fp16'] = (net(os.path.join(work, 'det16.ncnn'), True),
                    net(os.path.join(work, 'rec16.ncnn'), True))

def run(n, x):
    # ncnn.Mat(ndarray) borrows the array's memory: keep the array alive
    x = np.ascontiguousarray(x, dtype=np.float32)
    ex = n.create_extractor()
    ex.input('in0', ncnn.Mat(x))
    r, m = ex.extract('out0')
    assert r == 0
    return np.array(m).copy()

def det_ncnn_pre(img, W, H):
    # The C++ path: from_pixels_resize (bilinear) + substract_mean_normalize.
    m = ncnn.Mat.from_pixels_resize(img, ncnn.Mat.PixelType.PIXEL_BGR,
                                    img.shape[1], img.shape[0], W, H)
    m.substract_mean_normalize([127.5] * 3, [1 / 127.5] * 3)
    return np.array(m).copy()

def rec_ncnn_pre(crop):
    # height 48, width ceil(48*w/h), padded with 0.0 up to max(320, that).
    h, w = crop.shape[:2]
    W = int(48 * max(320 / 48, w / h))
    rw = min(int(np.ceil(48 * (w / float(h)))), W)
    m = ncnn.Mat.from_pixels_resize(np.ascontiguousarray(crop), ncnn.Mat.PixelType.PIXEL_BGR,
                                    w, h, rw, 48)
    m.substract_mean_normalize([127.5] * 3, [1 / 127.5] * 3)
    x = np.zeros((3, 48, W), np.float32); x[:, :, :rw] = np.array(m)
    return x

def text(p):
    return decode(p[None])[0][0]

stats = {k: dict(det_max=0., det_mean=[], det_bin=0., rec_max=0., argmax_bad=0, top5_bad=0,
                 text_bad=0, cols=0) for k in nets}
pre_stats = dict(det_tensor_max=0., det_bin=[], rec_text_bad=0, n=0, batch_differs=0,
                 nopad_differs=0)
n_det = n_rec = 0
diffs = []
for f in images:
    img = cv2.imread(f, cv2.IMREAD_COLOR)
    # det at the device setting (long side <= 960) and at full scan size
    # (long side <= 1500, ~1504 x 1216 for these pages).
    print(f'{os.path.basename(f)} ...', flush=True)
    for pre in (DetPreProcess(960, 'max', [.5] * 3, [.5] * 3),
                DetPreProcess(1500, 'max', [.5] * 3, [.5] * 3)):
        x = pre(img)
        ref = det_ort.run(None, {'x': x})[0][0, 0]
        for k, (dn, _) in nets.items():
            got = run(dn, x[0])[0]
            assert got.shape == ref.shape, (got.shape, ref.shape)
            d = np.abs(got - ref); s = stats[k]
            s['det_max'] = max(s['det_max'], float(d.max())); s['det_mean'].append(float(d.mean()))
            s['det_bin'] = max(s['det_bin'], float(((got > .3) != (ref > .3)).mean()))
        n_det += 1
        if pre.limit_side_len == 960:
            H, W = x.shape[2:]
            xn = det_ncnn_pre(img, W, H)
            pre_stats['det_tensor_max'] = max(pre_stats['det_tensor_max'], float(np.abs(xn - x[0]).max()))
            gotn = run(nets['fp32'][0], xn)[0]
            pre_stats['det_bin'].append(float(((gotn > .3) != (ref > .3)).mean()))
    # rec on every text line RapidOCR finds on the page, cropped its way
    boxes, _ = eng.text_det(img)
    if boxes is None or len(boxes) == 0:
        continue
    crops = eng.get_crop_img_list(img, eng.sorted_boxes(boxes))
    # RapidOCR recognises 6 lines per batch padded to the widest, so a line's
    # text depends on its neighbours; the reference is one line per batch.
    batched = [t for t, _ in eng.text_rec(crops)[0]]
    rapid = [eng.text_rec([c])[0][0][0] for c in crops]
    pre_stats['batch_differs'] += sum(a != b for a, b in zip(batched, rapid))
    for crop, rt in zip(crops, rapid):
        crop = np.ascontiguousarray(crop)
        h, w = crop.shape[:2]
        x = eng.text_rec.resize_norm_img(crop, max(320 / 48, w / h))[None].astype(np.float32)
        ref = rec_ort.run(None, {'x': x})[0][0]
        rt0 = text(ref)
        assert rt0 == rt, ('tensor path != RapidOCR', rt0, rt)
        for k, (_, rn) in nets.items():
            got = run(rn, x[0])
            assert got.shape == ref.shape, (got.shape, ref.shape)
            s = stats[k]
            s['rec_max'] = max(s['rec_max'], float(np.abs(got - ref).max()))
            s['cols'] += ref.shape[0]
            s['argmax_bad'] += int((got.argmax(1) != ref.argmax(1)).sum())
            t5 = lambda p: np.argsort(-p, 1)[:, :5]
            s['top5_bad'] += int((t5(got) != t5(ref)).any(1).sum())
            if text(got) != rt0:
                s['text_bad'] += 1; diffs.append((k, f, rt0, text(got)))
        xn = rec_ncnn_pre(crop)
        if text(run(nets['fp32'][1], xn)) != rt0:
            pre_stats['rec_text_bad'] += 1
        # no padding at all (ncnn takes any width): how much does it change?
        rw = xn.shape[2] if w / h >= 320 / 48 else int(np.ceil(48 * w / h))
        if text(run(nets['fp32'][1], x[0][:, :, :rw])) != rt0:
            pre_stats['nopad_differs'] += 1
        pre_stats['n'] += 1
        n_rec += 1

print(f'images {len(images)}  det runs {n_det}  rec lines {n_rec}')
for k, s in stats.items():
    print(f'[{k}] det prob max|d| {s["det_max"]:.2e} mean|d| {np.mean(s["det_mean"]):.2e} '
          f'worst >0.3 mask disagreement {s["det_bin"]*100:.4f}%')
    print(f'[{k}] rec prob max|d| {s["rec_max"]:.2e}  argmax differs {s["argmax_bad"]}/{s["cols"]} cols  '
          f'top-5 order differs {s["top5_bad"]}  text differs {s["text_bad"]}/{n_rec} lines')
print(f'[ncnn preprocessing] det tensor max|d| vs cv2 {pre_stats["det_tensor_max"]:.3f}  '
      f'mask disagreement mean {np.mean(pre_stats["det_bin"])*100:.3f}% '
      f'max {np.max(pre_stats["det_bin"])*100:.3f}%  '
      f'rec text differs {pre_stats["rec_text_bad"]}/{pre_stats["n"]} lines')
print(f'[padding] RapidOCR batch-of-6 text != batch-of-1 on {pre_stats["batch_differs"]} lines; '
      f'unpadded (width = resized width) != padded-to-320 on {pre_stats["nopad_differs"]} lines')
for d in diffs[:20]:
    print('  DIFF', *d)
# fp32 must decode identically column by column; probabilities agree to float
# rounding (ncnn's SIMD exp/attention accumulate differently: ~1e-4 det, ~1e-3 rec)
f32 = stats['fp32']
ok = (f32['text_bad'] == 0 and f32['argmax_bad'] == 0 and f32['det_bin'] < 1e-4
      and f32['det_max'] < 1e-3 and f32['rec_max'] < 5e-3)
print('OK' if ok else 'FAIL: fp32 ncnn does not match onnxruntime')
sys.exit(0 if ok else 1)
EOF
fi

echo "source models: $MODELS"
echo "$DET_SHA  $MODELS/ch_PP-OCRv4_det_infer.onnx" | sha256sum -c --quiet
echo "$REC_SHA  $MODELS/ch_PP-OCRv4_rec_infer.onnx" | sha256sum -c --quiet
cp "$MODELS/ch_PP-OCRv4_rec_infer.onnx" "$WORK/rec.onnx"

# Make the detector safe for fp16 weights (ncnn's default on ARM converts the
# weights to fp16 at load time). The DB head's 3x3 conv is followed by a
# BatchNorm whose running variance reaches 9.8e7, so the folded conv weights
# are 1e-4 .. 1e-9: 95% are fp16 subnormals and the probability map breaks
# (15-18% of the 0.3-mask flips). It is BN -> ReLU -> ConvTranspose, and
# ReLU(k z) = k ReLU(z) for k > 0, so scaling that BN's gamma and beta by
# k = 256 and the ConvTranspose's weights by 1/256 is exact (powers of two).
# That puts both layers' weights in fp16's normal range (<1% subnormal), and
# the largest activation in between (~2.7 * 256 on real pages) stays far
# below fp16's 65504. verify compares against the unpatched ONNX.
"$PY" - "$MODELS/ch_PP-OCRv4_det_infer.onnx" "$WORK/det.onnx" <<'EOF'
import sys, onnx, numpy as np
from onnx import numpy_helper
K = 256.0
m = onnx.load(sys.argv[1])
nodes = {n.name: n for n in m.graph.node}
users = {}
for n in m.graph.node:
    for i in n.input:
        users.setdefault(i, []).append(n)
const = {n.output[0]: n for n in m.graph.node if n.op_type == 'Constant'}
bn, relu, deconv = (nodes['p2o.BatchNormalization.1'], nodes['p2o.Relu.10'],
                    nodes['p2o.ConvTranspose.0'])
assert [u.name for u in users[bn.output[0]]] == [relu.name]
assert [u.name for u in users[relu.output[0]]] == [deconv.name]
def scale(name, f):
    assert len(users[name]) == 1, name           # not shared with another op
    t = const[name].attribute[0].t
    a = numpy_helper.to_array(t).astype(np.float32) * np.float32(f)
    t.CopyFrom(numpy_helper.from_array(a, t.name))
scale(bn.input[1], K)                            # gamma
scale(bn.input[2], K)                            # beta
scale(deconv.input[1], 1 / K)                    # ConvTranspose weight
onnx.checker.check_model(m)
onnx.save(m, sys.argv[2])
EOF

# Both inputs have dynamic H and W; two input shapes make pnnx keep them
# dynamic ([1,3,?,?] for det, [1,3,48,?] for rec). fp16=0 keeps the weights
# fp32 on disk (ncnn may still store them as fp16 at load time on ARM).
echo "pnnx $PNNX_VER ..."
(cd "$WORK" && nice -n 10 "$PNNX" det.onnx inputshape=[1,3,960,640] inputshape2=[1,3,480,736] \
    fp16=0 > det.log 2>&1) || { tail -20 "$WORK/det.log"; exit 1; }
(cd "$WORK" && nice -n 10 "$PNNX" rec.onnx inputshape=[1,3,48,320] inputshape2=[1,3,48,800] \
    fp16=0 > rec.log 2>&1) || { tail -20 "$WORK/rec.log"; exit 1; }
# fp16-weight variants for the verify step's comparison only (not shipped)
(cd "$WORK" && nice -n 10 "$PNNX" det.onnx inputshape=[1,3,960,640] inputshape2=[1,3,480,736] \
    fp16=1 ncnnparam=det16.ncnn.param ncnnbin=det16.ncnn.bin ncnnpy=det16_ncnn.py > det16.log 2>&1)
(cd "$WORK" && nice -n 10 "$PNNX" rec.onnx inputshape=[1,3,48,320] inputshape2=[1,3,48,800] \
    fp16=1 ncnnparam=rec16.ncnn.param ncnnbin=rec16.ncnn.bin ncnnpy=rec16_ncnn.py > rec16.log 2>&1)

mkdir -p "$OUT"
cp "$WORK/det.ncnn.param" "$OUT/ppocrv4_det.param"
cp "$WORK/det.ncnn.bin"   "$OUT/ppocrv4_det.bin"
cp "$WORK/rec.ncnn.param" "$OUT/ppocrv4_rec.param"
cp "$WORK/rec.ncnn.bin"   "$OUT/ppocrv4_rec.bin"

# The recogniser's dictionary, from the ONNX metadata 'character' that
# RapidOCR reads: one character per line, 6623 lines, UTF-8, '\n' endings.
# Output class k: 0 = CTC blank, 1..6623 = line k (1-based), 6624 = ' '.
"$PY" - "$WORK/rec.onnx" "$OUT/ppocrv4_keys.txt" <<'EOF'
import sys, onnx
m = onnx.load(sys.argv[1])
chars = {p.key: p.value for p in m.metadata_props}['character'].split('\n')
assert len(chars) == 6623 and all(len(c) == 1 for c in chars) and len(set(chars)) == 6623
assert m.graph.output[0].type.tensor_type.shape.dim[2].dim_value == len(chars) + 2
with open(sys.argv[2], 'w', encoding='utf-8', newline='\n') as f:
    f.write('\n'.join(chars) + '\n')
EOF

(cd "$OUT" && sha256sum ppocrv4_det.param ppocrv4_det.bin ppocrv4_rec.param ppocrv4_rec.bin ppocrv4_keys.txt)
echo "done: $OUT"
