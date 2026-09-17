# -*- coding: utf-8 -*-
"""
music_to_mub.py — 把 5 首外部来源的蜂鸣器谱转换为 .mub 音乐固件
(布局见 module/music_format.h: 36B头 + 曲目表16B/曲 + 名称区 + {u16 freq,u16 dur})
输出: tools/music/music_pack.mub(5首合成) + 每首单独 .mub
"""
import os, re, struct, zlib

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'music')
os.makedirs(OUT_DIR, exist_ok=True)

MAGIC = 0x3142554D  # 'MUB1'
VER = 1
HDR = 0x24
TRK_ENTRY = 16


def build_mub(tracks, names):
    n = len(tracks)
    name_bytes = b''
    name_pos = []
    for nm in names:
        name_pos.append(len(name_bytes))
        name_bytes += nm.encode('utf-8') + b'\x00'
    blobs = []
    for tr in tracks:
        b = b''
        for f, d in tr:
            b += struct.pack('<HH', f, int(d) & 0xFFFF)
        blobs.append(b)
    name_off = HDR + TRK_ENTRY * n
    tbl_off = HDR
    note_start = name_off + len(name_bytes)
    off = note_start
    entries = []
    for i, blob in enumerate(blobs):
        entries.append((off, len(blob), name_pos[i], len(names[i].encode('utf-8'))))
        off += len(blob)
    file_size = off
    # header (crc field=0, patched after)
    hdr = struct.pack('<IIHHIIIIII', MAGIC, VER, n, 0, file_size, 0,
                      name_off, len(name_bytes), tbl_off, 0)
    body = hdr
    for (no, ns, noff, nlen) in entries:
        body += struct.pack('<IIIHH', no, ns, noff, nlen, 0)
    body += name_bytes
    for blob in blobs:
        body += blob
    crc = zlib.crc32(body) & 0xFFFFFFFF
    body = body[:0x10] + struct.pack('<I', crc) + body[0x14:]
    return body


def save(name, tracks):
    names = [t[0] for t in tracks]
    data = build_mub([t[1] for t in tracks], names)
    p = os.path.join(OUT_DIR, name)
    with open(p, 'wb') as f:
        f.write(data)
    print('%-28s %6d bytes  %d tracks' % (name, len(data), len(tracks)))
    return data


# =====================================================================
# 1) 群青 — 51定时器重装载表 (11.0592MHz, /12, mode1)
# =====================================================================
RELOAD = [0,
63628,63731,63835,63928,64021,64103,64185,64260,64331,64400,64463,64528,
64580,64633,64684,64732,64777,64820,64860,64898,64934,64968,65000,65030,
65058,65085,65110,65134,65157,65178,65198,65217,65235,65252,65268,65283]

QUNQING_MUSIC = [0,0,0]  # placeholder (filled below by parser)

QUNQING_RAW = """M3,4,P,2,M2,2,M3,2,M6,2,M7,2,H1,2,M7,4,P,2,H1,2,M7,2,M5,2,M3,2,M2,2,
M3,4,P,2,M2,2,M3,2,M6,2,M5,2,M3,2,M1,4,P,4,L6,2,L7,2,M1,2,M2,2,
M3,4,P,2,M2,2,M3,4,M6,2,M5_,2,M5_,2,M6,4,M7,2,P,2,M3,2,M2,2,M1,2,
M2,2,M2,2,M1,2,M2,4,M5,2,M4,2,M4,2,M4,2,M3,2,M3,4,P,4,M1,2,M2,2,
M3,4,P,2,M2,2,M3,2,M6,2,M7,2,H1,2,M7,4,P,2,H1,2,M7,2,M5,2,M2,2,M2_,2,
M3,4,P,2,M2,2,M3,2,M6,2,M5,2,M3,2,M1,4,P,2,M2,2,M3,2,M3,2,M2,2,M1,2,
M1,4,P,2,M2,2,M3,2,M3,2,M2,2,M1,2,M1,4,P,2,L5,2,L6,2,M1,2,M1,4,
M6,2,H1,4,M6,2,H1,4,H1,2,H2,2,H3,2,H4,2,H3,2,P,2,H1,2,H2,2,H3,2,H4,2,
H3,3,H1,1,H1,2,H1,2,P,2,H2,2,H3,2,H4,2,H3,3,H1,1,H1,2,H1,4,H2,2,H1,2,P,2,
M6,2,H1,4,M6,2,H1,4,H1,2,H2,2,H3,2,H4,2,H3,2,P,2,H1,2,H2,2,H3,2,H4,2,
H5,3,H2,1,H2,2,H1,4,H2,2,H1,4,P,8,H3,8,H1,2,M6,2,M7,2,H1,2,P,2,M6,2,M7,2,H1,2,
P,2,M7,2,H1,2,H2,2,P,2,M5,2,M6,2,M7,2,M7,2,M5,2,M6,2,M7,4,M5,2,M2,2,M2,2,
M2,2,M5,2,M2,2,M3,2,P,2,M6,2,M5,2,M1,2,P,2,M6,2,M5,2,M1,2,P,2,M1,2,M6,2,M5_,2,
P,2,M5_,2,H2,2,M7,2,P,2,M7,2,H4,2,H3,2,H2,4,H1,2,H2,4,H5,2,H4,2,H4,2,
H4,2,H3,2,H3,4,P,2,M6,2,M7,2,H1,2,P,2,M6,2,M7,2,H1,2,P,2,M6,2,M7,2,H1,2,
P,2,M7,2,H1,2,H2,2,P,2,M5,2,M6,2,M7,2,M7,2,M5_,2,M7,2,H4,4,H3,2,H2,2,H1,2,
M7,2,H1,2,P,2,M6,4,M6,2,M5,2,M1,2,P,2,M6,2,M5,2,M1,2,P,2,L6,2,L6,2,M1,2,
M4,2,M3,2,M4,2,M5,2,M3,2,M2,2,M1,2,P,2,M6,4,M5,4,M3,2,M4,2,M5,2,M3,2,
M3,2,M1,4,M1,2,M3,2,M4,2,M5,2,M3,2,M3,2,M1,4,M1,2,M3,2,M4,2,M5,2,H1,2,
H1,2,H2,2,H3,2,H4,2,H3,2,M5,2,H1,4,M6,4,M5,4,M3,2,M4,2,M5,2,M3,2,
M3,2,M2,2,M3,2,M6,2,M5_,2,M7,2,H3,4,M3,4,P,2,M2,2,M3,2,M6,2,M7,2,H1,2,
M7,4,P,2,H1,2,M7,2,M5,2,M3,2,M2,2,M3,4,P,2,M2,2,M3,2,M6,2,M5,2,M3,2,
M1,4,P,4,L6,2,L7,2,M1,2,M2,2,M3,4,P,2,M2,2,M3,4,M6,2,M5_,2,
M5_,2,M6,4,M7,2,P,2,M3,2,M2,2,M1,2,M2,2,M2,2,M1,2,M2,4,M5,2,M4,2,M4,2,
M4,2,M3,2,M3,4,P,4,M1,2,M2,2,M3,4,P,2,M2,2,M3,2,M6,2,M7,2,H1,2,
M7,4,P,2,H1,2,M7,2,M5,2,M2,2,M2_,2,M3,4,P,2,M2,2,M3,2,M6,2,M5,2,M3,2,
M1,4,P,2,M2,2,M3,2,M3,2,M2,2,M1,2,M1,4,P,2,M2,2,M3,2,M3,2,M2,2,M1,2,
M1,4,P,2,M6,2,M5,2,M3,2,M2,2,M3,2,M1,16,P,8,H3,8,
H1,2,M6,2,M7,2,H1,2,P,2,M6,2,M7,2,H1,2,P,2,M7,2,H1,2,H2,2,P,2,M5,2,M6,2,M7,2,
M7,2,M5,2,M6,2,M7,4,M5,2,M2,2,M2,2,M2,2,M5,2,M2,2,M3,2,P,2,M6,2,M5,2,M1,2,
P,2,M6,2,M5,2,M1,2,P,2,M1,2,M6,2,M5_,2,P,2,M5_,2,H2,2,M7,2,P,2,M7,2,H4,2,H3,2,
H2,4,H1,2,H2,4,H5,2,H4,2,H4,2,H4,2,H3,2,H3,4,P,2,M6,2,M7,2,H1,2,
P,2,M6,2,M7,2,H1,2,P,2,M6,2,M7,2,H1,2,H1,2,M7,2,H1,2,H2,2,P,2,M5,2,M6,2,M7,2,
P,2,M5_,2,M7,2,H4,4,H3,2,H2,2,H1,2,H2,2,H1,2,P,2,M6,4,M6,2,M5,2,M1,2,
P,2,M6,2,M5,2,M1,2,P,2,L6,2,L6,2,M1,2,M4,2,M3,2,M4,2,M5,2,M3,2,P,2,M2,4,
M1,3,M1,1,M1,2,L6,2,M3,2,P,2,L6,2,L7,2,M1,3,M1,1,M1,2,L6,2,M3,2,P,2,M3,2,M4,2,
M5,2,M4,2,M3,2,M4,2,M3,2,M2,2,M1,2,M2,2,M1,3,M1,1,M1,2,L6,2,M3,2,P,2,M2,4,
M1,3,M1,1,M1,2,L6,2,M3,2,P,2,L6,2,L7,2,M1,3,M1,1,M1,2,L6,2,M3,2,P,2,M3,2,M4,2,
M5,2,M4,2,M3,2,M4,2,M5_,2,M3,1,M5,1,M5,1,M7,2,M7,1,M7,2,H1,2,M7,2,M5,2,M3,2,P,2,M3,2,M4,2,
M3,2,M1,2,M1,1,H6,2,M1,1,P,2,M3,2,M4,2,M5,2,M3,2,M1,2,M1,1,M2,2,M1,1,P,2,M3,2,M3,2,M4,2,
M5,2,M4,2,M3,2,M4,2,M5_,2,H3,2,H2,2,H2,2,H2,2,H1,2,H2,2,H1,2,H3,2,P,2,H2,4,
H1,4,H3,2,M6,2,P,2,M6,2,M6,2,M7,2,H1,4,H3,2,H2,2,P,2,H1,2,M7,2,H1,2,
H1,8,P,8,H1,2,M6,2,M7,2,H1,2,P,2,M6,2,M7,2,H1,2,H1,2,M7,2,H1,2,H2,2,P,2,M7,2,M6,2,M3,2,
M5,4,P,2,M3,2,M3,2,M5,2,M6_,2,M6,2,P,6,M6_,2,M6,2,M5,2,M4,2,M3,2,
M3,4,L6,2,M1,2,P,2,L6,2,M1,2,M3,2,M2,4,P,2,M1,2,M2,2,M7,2,M6,2,M3,2,
M5,4,P,2,M3,2,M5,2,M6,2,M6_,2,M7,2,M6_,2,M6,6,P,2,M6,2,M7,2,H1,2,
P,2,M6,2,M7,2,H1,2,P,2,M6,2,M7,2,H1,2,H1,2,M7,2,H1,2,H2,2,P,2,M5,2,M6,2,M7,2,
P,2,M5_,2,M7,2,H4,4,H3,2,H2,2,H1,2,H7,2,H1,2,P,2,M6,4,M6,2,M5,2,M1,2,
P,2,M6,2,M5,2,M1,2,P,2,L6,2,L6,2,M1,2,M4,2,M3,2,M4,2,M5,2,M3,2,M2,2,M1,2,P,2"""

NOTE_MAP = {'L1': 1, 'L1_': 2, 'L2': 3, 'L2_': 4, 'L3': 5, 'L4': 6, 'L4_': 7,
            'L5': 8, 'L5_': 9, 'L6': 10, 'L6_': 11, 'L7': 12,
            'M1': 13, 'M1_': 14, 'M2': 15, 'M2_': 16, 'M3': 17, 'M4': 18,
            'M4_': 19, 'M5': 20, 'M5_': 21, 'M6': 22, 'M6_': 23, 'M7': 24,
            'H1': 25, 'H1_': 26, 'H2': 27, 'H2_': 28, 'H3': 29, 'H4': 30,
            'H4_': 31, 'H5': 32, 'H5_': 33, 'H6': 34, 'H6_': 35, 'H7': 36}
NOTE_REV = {v: k for k, v in NOTE_MAP.items()}


def parse_note_dur_array(text, note_resolver, unit_ms, end_marker=None):
    """parse 'NAME,dur,NAME,dur,...,end' pairs (name may be P=rest)"""
    toks = [t.strip() for t in re.split(r'[,]', text.replace('\n', ' '))]
    out = []
    i = 0
    while i + 1 < len(toks):
        name = toks[i]
        if name in ('', '//', None):
            i += 1
            continue
        if name == end_marker:
            break
        dur = toks[i + 1]
        try:
            du = float(dur)
        except ValueError:
            i += 2
            continue
        if name == 'P':
            freq = 0
        else:
            freq = note_resolver(name)
        out.append((freq, int(round(du * unit_ms))))
        i += 2
    return out


def qunqing_freq(name):
    idx = NOTE_MAP[name]
    if idx == 0:
        return 0
    ticks = 65536 - RELOAD[idx]
    return int(round(921600.0 / (2.0 * ticks))) if ticks > 0 else 0


def solo_freq(name):
    idx = NOTE_MAP[name]
    if idx == 0:
        return 0
    ticks = 65536 - RELOAD[idx]
    return int(round(921600.0 / (2.0 * ticks))) if ticks > 0 else 0


# =====================================================================
# 2) 孤独摇滚 — STM32 tone[] = 1MHz ARR(µs), Music[] {index, unit}, dur=77.5*unit
# =====================================================================
SOLO_TONE = [0,
3817,3610,3401,3215,3030,2865,2703,2551,2410,2272,2146,2024,
1912,1805,1703,1608,1517,1432,1351,1275,1203,1136,1073,1012,
956,902,851,803,758,715,676,637,602,568,536,506,100]

SOLO_MUSIC_RAW = """H1,2,M7,2,M6,2,M7,4,M1,2,M2,2,M3,4,M1,4,L7,4,L7,2,L7,4,
H1,2,M7,2,M6,2,M7,4,M2,2,M3,4,M1,2,M5,4,M6,2,M5,4,
H1,2,M7,2,M6,2,M7,4,M1,2,M2,2,M3,4,M1,4,L7,4,L7,2,L7,4,
H2,4,H3,4,M6,4,H1,2,H2,4,H3,4,H5,4,H3,4,
H6,4,H5,4,H2,2,H3,4,H3,4,M6,4,M5,2,M6,4,M6,4,
M6,4,M6,2,M6,2,M6,2,M5,2,M5,2,M5,2,M3,4,M5,4,M3,4,M5,4,M3,6,
M5,2,M3,6,M5,2,M3,6,M5,2,M3,6,M2,2,M1,10,
M1,4,H1,4,M7,4,M5,4,M3,4,M2,4,M3,4,M2,4,M1,4,M3,6,M3,2,M2,8,
M6,4,M6,2,M6,2,M6,2,M5,2,M5,2,M5,2,M3,4,M5,4,M3,4,M5,4,M3,6,
M5,2,M3,6,M5,2,M3,6,M5,2,M3,6,M2,2,M1,10,
M1,4,H1,4,M7,4,M5,4,M3,4,M2,4,M3,4,M2,4,M1,4,M3,6,M4,2,M2,8,
M2,6,M1,2,M1,6,M1,2,M2,6,M1,2,M1,6,M1,2,M2,4,M1,4,M2,4,M5,4,M2,6,M1,2,M1,4,M1,6,
M1,2,M2,6,M1,2,M1,4,M1,4,M2,6,M1,2,M1,4,M1,4,M2,4,M1,4,M2,4,M5,4,M6,4,M5,2,M5,2,M5,2,
M5,2,M6,6,M5,2,M5,4,M5,4,M6,6,M5,2,M5,4,M5,4,M6,4,M5,4,M6,4,H1,4,M7,4,M5,4,M3,4,
M5,4,M1,12,M3,4,M2,2,M1,2,M1,6,M2,4,M3,6,M2,2,M2,8,M4,6,M3,2,M3,12,
H1,4,M7,4,M5,4,M5,2,M6,2,M6,4,M5,2,M6,2,M6,4,M7,4,H1,4,H2,4,M7,4,M7,4,H1,4,M7,4,M5,4,M3,4,
M3,4,M6,2,M5,2,M3,2,M1,2,M2,2,M3,4,M1,2,M2,2,M3,2,M1,4,M2,2,M3,2,M1,4,M2,2,M3,2,M5,2,M3,2,M2,4,M3,4,M2,4,M1,4,M3,8,
M6,4,M5,4,M5,2,M6,2,M6,4,M5,2,M6,2,M6,4,M7,4,H1,4,H2,4,M7,4,M5,4,H4,6,H3,2,H3,4,
H1,4,H2,4,H3,4,H4,2,H3,2,H3,2,H2,2,H1,2,M7,2,M7,4,M6,2,M7,2,M7,4,H1,2,H1,4,H1,16,P,8,
H1,4,H2,4,H3,4,H4,2,H3,2,H3,2,H2,2,H1,2,M7,8,P,2,M5,2,M7,4,H1,4,P,16"""


def solo_freq2(name):
    idx = NOTE_MAP[name]
    if idx == 0 or idx >= len(SOLO_TONE):
        return 0
    p = SOLO_TONE[idx]
    if p == 0:
        return 0
    return int(round(1000000.0 / p))


# =====================================================================
# 3) 恋爱吧少女 — 简谱 (1=#C), 半音偏移度, ()低八度 【】高八度, b=降, -=延长
# 2026-09-17: 简谱内嵌(不再依赖外部 txt, 原外部文件内容不对导致单音)
# =====================================================================
JIANPU_TEXT = """1=#C
BPM=112
34b55 3 (77)432
34b55 3 (77)【1】53
34b55 3 11 543 41146
【1】641-34b55

11121234321
1112121(767)
11121234321
(67)1(6)1(6)1(6)1 b65
3455531 1(7)1(6)32
345553 b765654434
56544345
b64b64b6【1】7-
2567526 65b55
3b55 2b55 656576
7526 65b55 3b55555
2b5565b556 b55-
5b5565b557
3b55 b35【1】765-

34b55 3 (77)432
34b55 3 (77)【1】5
34b55 3 11 543
4114567【1143】"""


def jianpu_parse(text):
    # key offset: "1=#C" -> C# = +1 ; "1=C" -> 0 ; "1=bB" -> -2
    m = re.search(r'1=([#b]?)([A-G])', text)
    key_off = 0
    if m:
        acc = m.group(1)
        note = m.group(2)
        semis = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
        key_off = semis[note] + (1 if acc == '#' else -1 if acc == 'b' else 0)
    # 去除注释行
    lines = []
    for ln in text.splitlines():
        s = ln.strip()
        if not s or s.startswith('/') or s.startswith('//'):
            continue
        if re.match(r'^\d\s*=', s):
            continue
        lines.append(s)
    major = [0, 2, 4, 5, 7, 9, 11]
    out = []          # (freq, dur_units)
    last_freq = 0
    dur = 250.0       # 每拍时长 ms
    bpm = None        # BPM=112: 每行=1小节=4拍, 音符均分, 时值=240000/BPM/行音符数
    line_notes = 0
    cur = []          # pending note with accumulated units
    for ln in lines:
        m = re.match(r'^D\s*=\s*([0-9]+(?:\.[0-9]+)?)$', ln)
        if m:
            dur = float(m.group(1))   # 段落调速指令: "D=200" 改变其后音符时值(ms)
            continue
        m = re.match(r'^BPM\s*=\s*([0-9]+(?:\.[0-9]+)?)$', ln)
        if m:
            bpm = float(m.group(1))   # 对音轨模式: 每行=1小节=4拍
            continue
        line_notes = len(re.findall(r'\d', re.sub(r'[b#\-]', '', re.sub(r'[()【】]', '', ln))))
        note_dur = dur
        if bpm and line_notes > 0:
            note_dur = 240000.0 / bpm / line_notes
        i = 0
        while i < len(ln):
            c = ln[i]
            if c == '-':                      # 延长前一音
                if cur:
                    cur[1] += note_dur
                i += 1
                continue
            if c == ' ':                      # 空格: 提交当前音(若存在)
                i += 1
                continue
            if c == '(':                      # 低八度组
                j = i + 1
                while j < len(ln) and ln[j] != ')':
                    j += 1
                grp = ln[i + 1:j]
                for g in grp:
                    if g.isdigit():
                        deg = int(g)
                        freq = 261.6256 * 2 ** ((key_off + major[deg - 1] - 12) / 12.0)
                        if cur:
                            out.append((cur[0], int(cur[1])))
                        cur = [int(round(freq)), note_dur]
                    elif g == 'b' or g == '#':
                        pass  # handled by prefix marker below
                i = j + 1
                continue
            if c == '【':                      # 高八度组
                j = i + 1
                while j < len(ln) and ln[j] != '】':
                    j += 1
                grp = ln[i + 1:j]
                for g in grp:
                    if g.isdigit():
                        deg = int(g)
                        freq = 261.6256 * 2 ** ((key_off + major[deg - 1] + 12) / 12.0)
                        if cur:
                            out.append((cur[0], int(cur[1])))
                        cur = [int(round(freq)), note_dur]
                i = j + 1
                continue
            if c.isdigit():                   # 普通音符 (可能前导 b/#)
                deg = int(c)
                if i > 0 and ln[i - 1] == 'b':
                    off = -1
                elif i > 0 and ln[i - 1] == '#':
                    off = 1
                else:
                    off = 0
                freq = 261.6256 * 2 ** ((key_off + major[deg - 1] + off) / 12.0)
                if cur:
                    out.append((cur[0], int(cur[1])))
                cur = [int(round(freq)), note_dur]
                i += 1
                continue
            i += 1
        # 行尾处理：不强制提交, '-' 跨行继续延长
    if cur:
        out.append((cur[0], int(cur[1])))
    # 去掉尾部休止/过短音 (如注释残留)
    return out


# =====================================================================
# 4) Bad Apple — Arduino tone() 谱
# =====================================================================
BADAPPLE_TEXT = open(r'E:\启明星烘干箱\固件\BadApple.txt', encoding='utf-8-sig').read()

BA_BASE = {'L5': 311, 'LS5': 330, 'L6': 349, 'L7': 392, '1': 415, '2': 466,
           '3': 523, '5': 622, '6': 698, '7': 784, 'H1': 831}
BA_RAISED = {'L5': 330, 'LS5': 349, 'L6': 370, 'L7': 415, '1': 440, '2': 494,
             '3': 554, '5': 659, '6': 740, '7': 831, 'H1': 880}

BA_WHOLE = 240000.0 / 140.0   # ms

# 手动展开(按 loop() 顺序), 直接写 playNote 序列, 复用函数体
BA_FUNCS = {}


def ba_note_dur(name, d):
    if name in ('REST', 'NOTE_REST'):
        freq = 0
    else:
        k = name[5:] if name.startswith('NOTE_') else name
        freq = BA_CUR[k]
    ms = BA_WHOLE / abs(d)
    if d < 0:
        ms *= 1.5
    return (freq, int(round(ms)))


# 直接从源文件提取函数并解释执行
BA_GLOBALS = {}


def ba_collect():
    src = BADAPPLE_TEXT
    # 提取所有函数体 (处理嵌套花括号)
    funcs = {}
    for m in re.finditer(r'void\s+(\w+)\s*\(([^)]*)\)\s*\{', src):
        name = m.group(1)
        depth = 1
        i = m.end()
        while depth > 0:
            c = src[i]
            if c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
            i += 1
        body = src[m.end():i - 1]
        funcs[name] = body
    return funcs


def ba_exec_stmt(stmt, funcs, stack):
    stmt = stmt.strip()
    if not stmt:
        return
    if stmt.startswith('playNote'):
        m = re.match(r'playNote\(\s*(\w+)\s*,\s*(-?[\d.]+)\s*\)', stmt)
        if m:
            name = m.group(1)
            d = float(m.group(2))
            stack.append(ba_note_dur(name, d))
        return
    if stmt.startswith('setBaseKey'):
        BA_CUR = BA_BASE
        return
    if stmt.startswith('setRaisedKey'):
        BA_CUR = BA_RAISED
        return
    if stmt.startswith('delay'):
        return
    m = re.match(r'(\w+)\s*\(\s*([^)]*)\)', stmt)
    if m and m.group(1) in funcs:
        fname = m.group(1)
        args = [a.strip() for a in m.group(2).split(',') if a.strip()]
        ba_exec_body(funcs[fname], funcs, stack, args)
        return
    if stmt.startswith('for'):
        m = re.match(r'for\s*\(\s*\w+\s*=\s*(\d+)\s*;\s*\w+\s*<=\s*([^;]+?)\s*;\s*\w+\+\+\s*\)\s*\{(.*)\}', stmt, re.S)
        if m:
            lo = int(m.group(1))
            hi_expr = m.group(2).strip()
            if hi_expr in BA_ARGS:
                hi = int(BA_ARGS[hi_expr])
            else:
                hi = int(hi_expr)
            body = m.group(3)
            for _ in range(lo, hi + 1):
                ba_exec_body(body, funcs, stack, [])
        return


BA_CUR = BA_BASE
BA_ARGS = {}


def ba_exec_body(body, funcs, stack, args):
    global BA_ARGS, BA_CUR
    # 保存参数上下文 (支持 times/3 等)
    old_args = BA_ARGS
    BA_ARGS = {}
    for i, a in enumerate(args):
        BA_ARGS['p%d' % i] = a
    # 拆语句: 先处理顶层 for 与 playNote/函数调用 (不深入解析 C)
    # 简易: 按函数体里直接提取 playNote/for/调用
    idx = 0
    body2 = body
    while True:
        m = re.search(r'for\s*\(|playNote\(|set(?:Base|Raised)Key\(\)|\b(\w+)\s*\(', body2)
        if not m:
            break
        # 简单方式: 逐行执行
        break
    # 逐行（可能跨行）——这里用括号感知的语句切分
    stmts = split_stmts(body)
    for s in stmts:
        ba_exec_stmt(s, funcs, stack)
    BA_ARGS = old_args


def split_stmts(body):
    out = []
    depth = 0
    cur = ''
    for c in body:
        cur += c
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        elif c == ';' and depth == 0:
            out.append(cur)
            cur = ''
    if cur.strip():
        out.append(cur)
    return out


def bad_apple_notes():
    global BA_CUR
    funcs = ba_collect()
    stack = []
    BA_CUR = BA_BASE
    # 模拟 loop() 主体
    loop_body = None
    for name, body in funcs.items():
        if name == 'loop':
            loop_body = body
    if loop_body:
        ba_exec_body(loop_body, funcs, stack, [])
    return stack


# =====================================================================
# 5) LOVE_2000.mid — SMF 解析
# =====================================================================
def parse_midi(path):
    data = open(path, 'rb').read()
    assert data[:4] == b'MThd'
    (hlen, fmt, ntrks, division) = struct.unpack('>IHHH', data[4:14])
    pos = 8 + hlen   # SMF 头 = 8字节MThd + hlen
    tempo = 500000
    notes = []   # (start_tick, dur_tick, midi_note)
    for _ in range(ntrks):
        assert data[pos:pos + 4] == b'MTrk'
        (tlen,) = struct.unpack('>I', data[pos + 4:pos + 8])
        end = pos + 8 + tlen
        p = pos + 8
        tick = 0
        status = 0
        while p < end:
            # VLQ delta
            delta = 0
            while True:
                b = data[p]
                p += 1
                delta = (delta << 7) | (b & 0x7F)
                if not (b & 0x80):
                    break
            tick += delta
            st = data[p]
            if st & 0x80:
                status = st
                p += 1
            st = status
            if st == 0xFF:            # meta
                mtype = data[p]
                p += 1
                (mlen,) = struct.unpack('>B', data[p:p + 1])
                p += 1
                if mtype == 0x51 and mlen == 3:
                    tempo = int.from_bytes(data[p:p + 3], 'big')
                p += mlen
            elif (st & 0xF0) == 0xF0:  # sysex / common
                (sl,) = struct.unpack('>B', data[p:p + 1])
                p += 1 + sl
            else:
                hi = st & 0xF0
                if hi == 0x90 or hi == 0x80:
                    note = data[p]
                    vel = data[p + 1]
                    p += 2
                    if hi == 0x90 and vel > 0:
                        notes.append([tick, None, note])
                    else:
                        for n in notes:
                            if n[2] == note and n[1] is None:
                                n[1] = tick - n[0]
                                break
                elif hi == 0xC0 or hi == 0xD0:
                    p += 1
                else:
                    p += 2
        pos = end
    # tempo 平均
    return division, tempo, notes


def midi_melody(division, tempo, notes):
    if division & 0x8000:
        raise ValueError('SMPTE division not supported')
    # 过滤未关闭的音(给默认时长)
    evs = []
    for (s, d, n) in notes:
        if d is None:
            d = 480
        evs.append((s, n, 1))
        evs.append((s + d, n, -1))
    evs.sort(key=lambda x: x[0])
    active = {}
    out = []           # (freq, dur_ms)
    cur_time = 0
    i = 0
    while i < len(evs):
        t = evs[i][0]
        # 取当前最高音
        if active:
            f = max(active.keys())
            freq = int(round(440.0 * 2 ** ((f - 69) / 12.0)))
        else:
            freq = 0
        d_ms = (t - cur_time) * tempo / (division * 1000.0)
        if d_ms >= 1:
            out.append((freq, int(round(d_ms))))
        cur_time = t
        while i < len(evs) and evs[i][0] == t:
            _, n, sgn = evs[i]
            if sgn == 1:
                active[n] = active.get(n, 0) + 1
            else:
                active[n] = active.get(n, 0) - 1
                if active[n] <= 0:
                    del active[n]
            i += 1
    # 去掉开头连续休止与结尾休止
    return out


# =====================================================================
# 组装
# =====================================================================
if __name__ == '__main__':
    tracks = []

    # 群青
    qn = parse_note_dur_array(QUNQING_RAW, qunqing_freq, 100.0)
    print('群青 notes:', len(qn))
    tracks.append(('群青', qn))

    # 孤独摇滚
    solo = parse_note_dur_array(SOLO_MUSIC_RAW, solo_freq2, 77.5)
    print('孤独摇滚 notes:', len(solo))
    tracks.append(('孤独摇滚-蓝色星球', solo))

    # 恋爱吧少女
    jp = jianpu_parse(JIANPU_TEXT)
    print('恋爱吧少女 notes:', len(jp))
    tracks.append(('恋爱吧少女', jp))

    # Bad Apple
    ba = bad_apple_notes()
    print('Bad Apple notes:', len(ba))
    tracks.append(('Bad Apple', ba))

    # LOVE_2000
    div, tempo, notes = parse_midi(r'E:\启明星烘干箱\固件\LOVE_2000.mid')
    ml = midi_melody(div, tempo, notes)
    print('LOVE_2000 notes:', len(ml))
    tracks.append(('LOVE_2000', ml))

    # 单曲文件 + 打包
    for (nm, tr) in tracks:
        save('music_%s.mub' % nm.replace('-', '_'), [(nm, tr)])
    save('music_pack.mub', tracks)
    print('OK ->', OUT_DIR)