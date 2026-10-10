"""Helpers for drawing icons as filled polygons and writing them as SVG and HVIF."""
import math, struct

# ------------------------------------------------------------------ geometry
def ellipse(cx, cy, rx, ry, n=48, rot=0.0):
    pts = []
    c, s = math.cos(rot), math.sin(rot)
    for i in range(n):
        a = 2 * math.pi * i / n
        x, y = rx * math.cos(a), ry * math.sin(a)
        pts.append((cx + x * c - y * s, cy + x * s + y * c))
    return pts

def bezier(p0, p1, p2, p3, n=24):
    out = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                    u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
    return out

def tube(center, widths, caps=True):
    """A closed outline around a polyline whose width varies along it (widths: list, one per point)."""
    left, right = [], []
    n = len(center)
    for i, (x, y) in enumerate(center):
        a = center[max(i - 1, 0)]
        b = center[min(i + 1, n - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        d = math.hypot(dx, dy) or 1
        nx, ny = -dy / d, dx / d
        w = widths[i] / 2
        left.append((x + nx * w, y + ny * w))
        right.append((x - nx * w, y - ny * w))
    outline = left + right[::-1]
    return outline

def lerp_widths(a, b, n):
    return [a + (b - a) * i / (n - 1) for i in range(n)]

def grow(pts, amount):
    """Offset a roughly convex polygon outwards from its centroid (for outlines)."""
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    out = []
    for x, y in pts:
        dx, dy = x - cx, y - cy
        d = math.hypot(dx, dy) or 1
        out.append((x + dx / d * amount, y + dy / d * amount))
    return out

# ------------------------------------------------------------------ SVG
def svg(shapes):
    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 64 64"><defs>']
    gid = 0
    ref = {}
    for name, pts, st in shapes:
        if st[0] == 'grad':
            gid += 1
            ref[name] = gid
            _, p0, p1, stops = st
            out.append('<linearGradient id="g%d" gradientUnits="userSpaceOnUse" x1="%g" y1="%g" x2="%g" y2="%g">' % (gid, p0[0], p0[1], p1[0], p1[1]))
            for t, c in stops:
                out.append('<stop offset="%g" stop-color="rgb(%d,%d,%d)" stop-opacity="%g"/>' % (t, c[0], c[1], c[2], c[3] / 255))
            out.append('</linearGradient>')
    out.append('</defs>')
    for name, pts, st in shapes:
        d = 'M' + ' L'.join('%.2f,%.2f' % p for p in pts) + ' Z'
        if st[0] == 'solid':
            c = st[1]
            out.append('<path d="%s" fill="rgb(%d,%d,%d)" fill-opacity="%g"/>' % (d, c[0], c[1], c[2], c[3] / 255))
        else:
            out.append('<path d="%s" fill="url(#g%d)"/>' % (d, ref[name]))
    out.append('</svg>')
    return '\n'.join(out)

# ------------------------------------------------------------------ HVIF
def write_coord(buf, v):
    v = max(-128.0, min(192.0, v))
    if int(v * 100) == int(v) * 100 and -32.0 <= v <= 95.0:
        buf.append(int(v + 32))
    else:
        value = int((v + 128.0) * 102.0) | 32768
        buf.append(value >> 8)
        buf.append(value & 255)

def write_float24(buf, v):
    if v == 0:
        buf.extend([0, 0, 0]); return
    i = struct.unpack('<I', struct.pack('<f', v))[0]
    sign = (i & 0x80000000) >> 31
    exponent = ((i & 0x7f800000) >> 23) - 127
    mantissa = i & 0x007fffff
    if exponent >= 32 or exponent < -32:
        buf.extend([0, 0, 0]); return
    short = (sign << 23) | ((exponent + 32) << 17) | (mantissa >> 6)
    buf.extend([(short >> 16) & 255, (short >> 8) & 255, short & 255])

def hvif(shapes):
    # styles
    styles, style_index = [], {}
    for name, pts, st in shapes:
        key = repr(st)
        if key not in style_index:
            style_index[key] = len(styles)
            styles.append(st)
    buf = bytearray(b'ncif')
    buf.append(len(styles))
    for st in styles:
        if st[0] == 'solid':
            r, g, b, a = st[1]
            if a == 255:
                buf.append(3); buf.extend([r, g, b])
            else:
                buf.append(1); buf.extend([r, g, b, a])
        else:
            _, p0, p1, stops = st
            # gradient space: x runs -64..64 along the gradient; map it onto p0 -> p1
            dx, dy = p1[0] - p0[0], p1[1] - p0[1]
            length = math.hypot(dx, dy)
            s = length / 128.0
            ang = math.atan2(dy, dx)
            mid = ((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2)
            m = [s * math.cos(ang), s * math.sin(ang), -s * math.sin(ang), s * math.cos(ang), mid[0], mid[1]]
            has_alpha = any(c[3] != 255 for _, c in stops)
            flags = 2 | (0 if has_alpha else 4)
            buf.append(2); buf.append(0); buf.append(flags); buf.append(len(stops))
            for v in m: write_float24(buf, v)
            for t, c in stops:
                buf.append(int(round(t * 255)))
                buf.extend([c[0], c[1], c[2], c[3]] if has_alpha else [c[0], c[1], c[2]])
    # paths
    buf.append(len(shapes))
    for name, pts, st in shapes:
        if len(pts) > 255: raise SystemExit('too many points in ' + name)
        buf.append(2 | 8)          # closed, no curves
        buf.append(len(pts))
        for x, y in pts:
            write_coord(buf, x); write_coord(buf, y)
    # shapes
    buf.append(len(shapes))
    for i, (name, pts, st) in enumerate(shapes):
        buf.append(10)
        buf.append(style_index[repr(st)])
        buf.append(1)
        buf.append(i)
        buf.append(0)              # flags
    return bytes(buf)

