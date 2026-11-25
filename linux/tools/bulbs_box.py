# bulbs_box.py in.png EX EY EZ FOV out.png: draw the unit cube's edges and axes
import sys, numpy as np, itertools
from PIL import Image, ImageDraw, ImageFont
e=np.array([float(a) for a in sys.argv[2:5]]); im=Image.open(sys.argv[1]).convert('RGB'); P=im.size[0]
w=.5-e; w/=np.linalg.norm(w); r=np.cross(w,[0,0,1]); r/=np.linalg.norm(r); u=np.cross(r,w); f=P/2/np.tan(np.radians(float(sys.argv[5]))/2)
def pr(p):
    p=np.asarray(p,float)-e; z=p@w
    return (P/2+f*(p@r)/z,P/2-f*(p@u)/z) if z>1e-2 else None
d=ImageDraw.Draw(im); F=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',P//45)
# faces turned to the eye; an edge is hidden if neither of its faces is
vis=lambda ax,s:(e[ax]>1) if s else (e[ax]<0)
for a,b in itertools.combinations(list(itertools.product((0,1),repeat=3)),2):
    if sum(x!=y for x,y in zip(a,b))!=1: continue
    ax=[x!=y for x,y in zip(a,b)].index(True); front=any(vis(o,a[o]) for o in range(3) if o!=ax)
    pts=[pr(np.array(a)+(np.array(b)-np.array(a))*t) for t in np.linspace(0,1,81)]
    for k in range(80):
        if pts[k] and pts[k+1] and (front or k%4<2): d.line(pts[k]+pts[k+1],fill=(255,255,255) if front else (150,150,150),width=P//500)
for t,p in (('0',(0,0,0)),('X',(1,0,0)),('Y',(0,1,0)),('Z',(0,0,1))):
    q=pr(np.array(p)*1.06-np.array([.03]*3)*(t=='0'))
    if q: d.text(q,t,fill='yellow',font=F,anchor='mm',stroke_width=3,stroke_fill='black')
if len(sys.argv)>7: # outline of the slice x >= X0
    x=float(sys.argv[7]); c=[(x,0,0),(x,1,0),(x,1,1),(x,0,1),(x,0,0)]
    for a,b in zip(c,c[1:]): d.line(pr(a)+pr(b),fill=(90,140,220),width=P//700)
# Wythoff ends (0,1/phi,1) etc. must lie on the tips of the fans
g=2/(1+5**.5)
for p in () if len(sys.argv)>7 else set(itertools.permutations((0,g,1))):
    q=pr(p)
    if q: d.ellipse([q[0]-9,q[1]-9,q[0]+9,q[1]+9],outline='magenta',width=3)
im.save(sys.argv[6])
