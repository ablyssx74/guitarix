#!/usr/bin/env python3
"""The Guitarix icon for Haiku: an electric guitar at an angle. Writes guitarix.svg (preview) and guitarix.hvif."""
import math, sys
from iconlib import *

shapes = []
def solid(r, g, b, a=255): return ('solid', (r, g, b, a))
def vgrad(y0, y1, stops): return ('grad', (0, y0), (0, y1), stops)

ANGLE = math.radians(38)       # the guitar leans to the right
CX, CY = 25.0, 44.0            # where the body centre ends up
SCALE = 0.8

def place(pts):
    c, s = math.cos(ANGLE), math.sin(ANGLE)
    return [(CX + (x * c - y * s) * SCALE, CY + (x * s + y * c) * SCALE) for x, y in pts]

def rrect(x0, y0, x1, y1, r, n=5):
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for k in range(n + 1):
            a = math.radians(a0 + 90 * k / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts

def add(name, pts, style): shapes.append((name, place(pts), style))

LINE = (44, 8, 10, 255)
RED_TOP = (240, 40, 40, 255)
RED_MID = (214, 18, 26, 255)
RED_BOT = (158, 8, 18, 255)

lower = ellipse(0, 9, 16.5, 14.5, 48)
upper = ellipse(0, -6, 12.5, 11, 40)
# horn on the side of the neck gives the body its shape
body_gr = vgrad(14, 66, [(0, RED_TOP), (0.5, RED_MID), (1, RED_BOT)])

# soft shadow
shapes.append(('shadow', ellipse(32, 61, 22, 2.4), solid(20, 8, 0, 70)))

# neck and headstock (behind the body)
neck = rrect(-3.0, -52, 3.0, -6, 0.8)
fret = rrect(-2.3, -50, 2.3, -6, 0.5)
add('neck-line', [(x * 1.0, y) for x, y in rrect(-3.9, -53, 3.9, -4, 1.2)], solid(*LINE))
add('neck', neck, solid(232, 196, 128))
add('fretboard', fret, solid(244, 216, 160))
for i, y in enumerate((-47, -42, -37, -32, -27, -22, -17, -12)):
    add('fret%d' % i, rrect(-2.3, y, 2.3, y + 0.7, 0.2, 2), solid(176, 176, 172))
for i, y in enumerate((-45, -35, -25)):
    add('inlay%d' % i, ellipse(0, y, 0.9, 0.9, 10), solid(60, 40, 24))

head = [(-5.0, -53), (5.0, -53), (6.2, -60), (4.5, -66), (-4.5, -66), (-6.2, -60)]
add('head-line', grow(head, 1.0), solid(*LINE))
add('head', head, vgrad(30, 50, [(0, (240, 206, 140, 255)), (1, (214, 172, 100, 255))]))
for i, (x, y) in enumerate(((-4.6, -63), (-4.6, -59), (-4.6, -55), (4.6, -63), (4.6, -59), (4.6, -55))):
    add('peg%d' % i, ellipse(x, y, 1.1, 1.1, 10), solid(214, 218, 224))

# body
add('body-line-l', grow(lower, 1.1), solid(*LINE))
add('body-line-u', grow(upper, 1.1), solid(*LINE))
add('body-l', lower, body_gr)
add('body-u', upper, body_gr)
add('body-gloss', ellipse(-7, 4, 4.0, 9.5, 24, 0.5), solid(255, 255, 255, 70))

# the stripes: black and white bands across the body, cut off at the body's outline
def clip(subject, clipper):
    """Sutherland-Hodgman: the part of the polygon `subject` inside the convex polygon `clipper`."""
    area = sum(clipper[i][0] * clipper[(i + 1) % len(clipper)][1] - clipper[(i + 1) % len(clipper)][0] * clipper[i][1]
               for i in range(len(clipper)))
    sign = 1 if area > 0 else -1
    def inside(p, a, b): return sign * ((b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])) >= 0
    def cross(p, q, a, b):
        x1, y1, x2, y2 = p[0], p[1], q[0], q[1]
        x3, y3, x4, y4 = a[0], a[1], b[0], b[1]
        d = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4)
        t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / d
        return (x1 + t * (x2 - x1), y1 + t * (y2 - y1))
    out = subject
    for i in range(len(clipper)):
        a, b = clipper[i], clipper[(i + 1) % len(clipper)]
        inp, out = out, []
        if not inp:
            break
        prev = inp[-1]
        for cur in inp:
            if inside(cur, a, b):
                if not inside(prev, a, b):
                    out.append(cross(prev, cur, a, b))
                out.append(cur)
            elif inside(prev, a, b):
                out.append(cross(prev, cur, a, b))
            prev = cur
    return out

def band(cx, cy, deg, w):
    a = math.radians(deg)
    dx, dy = math.cos(a) * 40, math.sin(a) * 40
    nx, ny = -math.sin(a) * w, math.cos(a) * w
    return [(cx - dx + nx, cy - dy + ny), (cx + dx + nx, cy + dy + ny), (cx + dx - nx, cy + dy - ny), (cx - dx - nx, cy - dy - ny)]

BLACK = (22, 14, 14, 255)
WHITE = (246, 244, 238, 255)
# (centre x, centre y, angle, half width, colour): white bands run beside the black ones
# a lighter variant with fewer, thinner stripes (more red); use it by setting stripes = stripes_light
stripes_light = [
    (-7, 4, -55, 1.3, BLACK), (-4.9, 5.2, -55, 0.55, WHITE),
    (6, 12, 28, 1.4, BLACK), (4.2, 11.2, 28, 0.6, WHITE),
    (7, -7, -24, 1.1, BLACK), (5.8, -8.6, -24, 0.5, WHITE),
    (-9, 15, 66, 1.2, BLACK),
    (2, -2, 80, 0.9, BLACK), (4.2, -1, 80, 0.5, WHITE),
    (8, 19, -62, 1.0, BLACK), (10.2, 18.2, -62, 0.5, WHITE),
    (-3, 18, 4, 0.55, WHITE),
]
stripes = [
    (-6, 2, -52, 1.7, BLACK), (-3.2, 3.6, -52, 0.8, WHITE),
    (5, 9, 24, 1.9, BLACK), (2.6, 8.0, 24, 0.9, WHITE),
    (8, -7, -22, 1.5, BLACK), (7, -9.5, -22, 0.8, WHITE),
    (-10, 14, 62, 1.6, BLACK), (-7.6, 13.2, 62, 0.8, WHITE),
    (0, -2, 82, 1.2, BLACK), (3, 0, 82, 0.8, WHITE),
    (6, 19, -64, 1.4, BLACK), (9, 18, -64, 0.7, WHITE),
    (-4, -12, 36, 1.3, BLACK), (-6.5, -9.6, 36, 0.7, WHITE),
    (10, 3, 68, 0.9, WHITE), (-1, 17, 6, 0.8, WHITE), (-9, -4, -72, 0.8, WHITE),
]
for n, (cx, cy, deg, w, col) in enumerate(stripes):
    for part, outline in (('l', lower), ('u', upper)):
        piece = clip(band(cx, cy, deg, w), outline)
        if len(piece) >= 3:
            add('stripe%d%s' % (n, part), piece, solid(*col))

# one humbucker, the bridge, one knob
add('humbucker-line', grow(rrect(-9, 5.5, 9, 13.5, 1.4), 0.6), solid(10, 8, 8, 255))
add('humbucker', rrect(-9, 5.5, 9, 13.5, 1.4), solid(34, 30, 30))
for i in range(6):
    x = -6.2 + i * 2.5
    add('pole1_%d' % i, ellipse(x, 7.6, 0.6, 0.6, 8), solid(196, 198, 202))
    add('pole2_%d' % i, ellipse(x, 11.4, 0.6, 0.6, 8), solid(196, 198, 202))
add('bridge', rrect(-8.5, 18, 8.5, 20.4, 0.8), solid(200, 204, 212))
add('knob-line', ellipse(11.5, 15.5, 2.0, 2.0, 12), solid(10, 8, 8, 255))
add('knob', ellipse(11.5, 15.5, 1.5, 1.5, 12), solid(70, 66, 66))

# strings
for i in range(6):
    x = -2.0 + i * 0.8
    add('string%d' % i, rrect(x - 0.12, -52, x + 0.12, 19.5, 0.1, 2), solid(226, 226, 220, 235))

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'guitarix'
    open(out + '.svg', 'w').write(svg(shapes))
    data = hvif(shapes)
    open(out + '.hvif', 'wb').write(data)
    print(len(shapes), 'shapes,', len(data), 'bytes')
