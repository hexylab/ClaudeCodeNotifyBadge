# Clawd参照GIF → C言語スプライトヘッダー変換
from PIL import Image, ImageSequence
from collections import Counter
import math

STATES = ['idle', 'typing', 'happy', 'thinking', 'notification', 'error']

# 1) 全状態の色ヒストグラム
hist = Counter()
data = {}
for name in STATES:
    im = Image.open(f'clawd-{name}.gif')
    frames = [f.convert('RGBA') for f in ImageSequence.Iterator(im)]
    durs = []
    im.seek(0)
    try:
        while True:
            durs.append(im.info.get('duration', 70))
            im.seek(im.tell() + 1)
    except EOFError:
        pass
    data[name] = (frames, durs)
    for f in frames:
        for (r, g, b, a) in f.getdata():
            if a > 128:
                hist[(r, g, b)] += 1

# 2) 貪欲クラスタリングでGIF量子化ノイズを除去
clusters = []
for c, n in hist.most_common():
    for i, (rep, cnt) in enumerate(clusters):
        if math.dist(c, rep) < 32:
            clusters[i] = (rep, cnt + n)
            break
    else:
        clusters.append((c, n))
clusters.sort(key=lambda x: -x[1])
palette = [rep for rep, cnt in clusters]
cache = {}


def qidx(c):
    if c not in cache:
        cache[c] = min(range(len(palette)), key=lambda i: math.dist(c, palette[i]))
    return cache[c]


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


out = []
out.append('// Auto-generated Clawd sprite data - extracted 1:1 from reference pixel art GIFs')
out.append('#pragma once')
out.append('#include <stdint.h>')
out.append('')
out.append('typedef struct { const uint8_t* rle; uint32_t rleLen; } ClawdFrame;')
out.append('typedef struct {')
out.append('  uint16_t w, h; int16_t offX, offY;')
out.append('  uint8_t frameCount; uint8_t seqLen;')
out.append('  const ClawdFrame* frames; const uint8_t* seq; const uint16_t* durMs;')
out.append('} ClawdAnim;')
out.append('')
out.append('static const uint16_t CLAWD_PALETTE[%d] = { 0x0000, %s };' % (
    len(palette) + 1, ', '.join('0x%04X' % rgb565(*c) for c in palette)))
out.append('// palette RGB: ' + ' '.join(
    '%d=#%02X%02X%02X' % (i + 1, c[0], c[1], c[2]) for i, c in enumerate(palette)))
out.append('')

qa_frames = {}
totalbytes = 0
for name in STATES:
    frames, durs = data[name]
    W, H = frames[0].size
    qlist = []
    for f in frames:
        q = bytes(0 if px[3] <= 128 else qidx(px[:3]) + 1 for px in f.getdata())
        qlist.append(q)
    # 量子化後の全フレーム共通bbox
    minx, miny, maxx, maxy = W, H, 0, 0
    for q in qlist:
        for i, v in enumerate(q):
            if v:
                x, y = i % W, i // W
                minx = min(minx, x); maxx = max(maxx, x)
                miny = min(miny, y); maxy = max(maxy, y)
    cw, ch = maxx - minx + 1, maxy - miny + 1
    # 重複フレーム排除
    uniq = []; seen = {}; seq = []
    for q in qlist:
        crop = b''.join(q[(miny + y) * W + minx: (miny + y) * W + minx + cw] for y in range(ch))
        if crop not in seen:
            seen[crop] = len(uniq)
            uniq.append(crop)
        seq.append(seen[crop])
    qa_frames[name] = (uniq, cw, ch, W, H, minx, miny)
    # RLEエンコード (run長, パレット番号) の2バイト組。0=透過(背景)
    for fi, crop in enumerate(uniq):
        rle = bytearray()
        i = 0; n = len(crop)
        while i < n:
            v = crop[i]; run = 1
            while i + run < n and crop[i + run] == v and run < 255:
                run += 1
            rle += bytes((run, v))
            i += run
        totalbytes += len(rle)
        out.append('static const uint8_t %s_f%d[] PROGMEM = {%s};' % (
            name, fi, ','.join(str(b) for b in rle)))
    out.append('static const ClawdFrame %s_frames[] = { %s };' % (
        name, ', '.join('{%s_f%d, sizeof(%s_f%d)}' % (name, i, name, i) for i in range(len(uniq)))))
    out.append('static const uint8_t %s_seq[] = {%s};' % (name, ','.join(map(str, seq))))
    out.append('static const uint16_t %s_dur[] = {%s};' % (name, ','.join(map(str, durs))))
    out.append('static const ClawdAnim ANIM_%s = { %d, %d, %d, %d, %d, %d, %s_frames, %s_seq, %s_dur };' % (
        name.upper(), cw, ch, minx, miny, len(uniq), len(seq), name, name, name))
    out.append('')
    print(f'{name}: {cw}x{ch} off=({minx},{miny}) unique={len(uniq)} seq={len(seq)}')

open('clawd_sprites.h', 'w').write('\n'.join(out))
print('total RLE bytes:', totalbytes, '-> header text', len('\n'.join(out)) // 1024, 'KB')

# 品質確認画像: 左=オリジナル 右=量子化後 (2倍拡大)
for name in STATES:
    uniq, cw, ch, W, H, minx, miny = qa_frames[name]
    frames, _ = data[name]
    for tag, idx in [('a', 0), ('b', len(uniq) // 2)]:
        crop = uniq[idx]
        img = Image.new('RGB', (cw * 2 + 8, ch), (40, 0, 60))
        orig = frames[idx].crop((minx, miny, minx + cw, miny + ch)).convert('RGB')
        img.paste(orig, (0, 0))
        qi = Image.new('RGB', (cw, ch), (0, 0, 0))
        qp = qi.load()
        for y in range(ch):
            for x in range(cw):
                v = crop[y * cw + x]
                qp[x, y] = (0, 0, 0) if v == 0 else palette[v - 1]
        img.paste(qi, (cw + 8, 0))
        img = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
        img.save(f'qa_{name}_{tag}.png')
print('QA images saved')
