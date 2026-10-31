#!/usr/bin/env python3
"""Black & white Kobento mark: outlined isometric box (no lid) + solid black content
shapes, matching the provided reference. Flat plane drawn then tilted by one transform."""
import sys
import os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "brand")
BLACK = "#111418"
SW = 22  # outline stroke width (flat-plane units)

# oblique tilt params (tweak to match the reference)
ROT = float(sys.argv[1]) if len(sys.argv) > 1 else -7.0
SKX = float(sys.argv[2]) if len(sys.argv) > 2 else -10.0
SY  = float(sys.argv[3]) if len(sys.argv) > 3 else 0.84

# flat-plane box
BX, BY, BW, BH = 0, 0, 940, 620

def rr(x,y,w,h,r,fill="none",stroke="none",sw=0,extra=""):
    s=f' stroke="{stroke}" stroke-width="{sw}"' if stroke!="none" else ""
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" ry="{r}" fill="{fill}"{s} {extra}/>'
def circ(cx,cy,r,fill):
    return f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"/>'
def tri(pts,fill):
    return f'<polygon points="{" ".join(f"{a},{b}" for a,b in pts)}" fill="{fill}" stroke="{fill}" stroke-width="10" stroke-linejoin="round"/>'

def content():
    s=[]
    # outlined box: outer + inner rounded rect (the rim)
    s.append(rr(BX,BY,BW,BH,110, fill="#FFFFFF", stroke=BLACK, sw=SW))
    s.append(rr(BX+40,BY+40,BW-80,BH-80,78, fill="none", stroke=BLACK, sw=SW))
    # content (solid black) in the inner area
    s.append(tri([(150,250),(150,110),(275,185)], BLACK))              # triangle (play), top-left
    s.append(rr(330,120,360,46,23, fill=BLACK))                        # bar 1
    s.append(rr(330,196,300,46,23, fill=BLACK))                        # bar 2
    s.append(circ(205,430,78,BLACK))                                   # circle bottom-left
    s.append(circ(420,440,78,BLACK))                                   # circle bottom-center
    s.append(rr(610,300,165,150,34, fill=BLACK))                       # square bottom-right
    return "".join(s)

def build(path):
    cx,cy = BX+BW/2, BY+BH/2
    VBW,VBH = 1240, 980
    tx,ty = VBW/2, VBH/2
    g_tf = (f'translate({tx},{ty}) rotate({ROT}) skewX({SKX}) scale(1,{SY}) '
            f'translate({-cx},{-cy})')
    svg=[f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {VBW} {VBH}" width="{VBW}" height="{VBH}" role="img" aria-label="Kobento">',
         f'<rect x="0" y="0" width="{VBW}" height="{VBH}" fill="#FFFFFF"/>',
         f'<g transform="{g_tf}" stroke-linejoin="round">{content()}</g>',
         '</svg>']
    open(path,"w").write("\n".join(svg)+"\n")
    print("wrote",path,"rot",ROT,"skx",SKX,"sy",SY)

build(f"{OUT}/kobento-logo-black.svg")
