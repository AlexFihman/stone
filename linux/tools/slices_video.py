# slices_video.py RAWDIR PIXELS EX EY EZ FOV out.mp4 COUNT GROUP HOLD FADE: RAWDIR/0..COUNT-1
# are equal stone_bulbs slices in x, GROUP of them are shown together, for HOLD frames
# (if RAWDIR/smax exists: slices of x+y+z by stone_diag) with FADE crossfade frames between; same colours and exposure for all frames.
import sys, subprocess, numpy as np, itertools
from PIL import Image, ImageDraw, ImageFont
D,n=sys.argv[1],int(sys.argv[2]); e=np.array([float(a) for a in sys.argv[3:6]]); P=n//2; fov=float(sys.argv[6])
w=.5-e; w/=np.linalg.norm(w); r=np.cross(w,[0,0,1]); r/=np.linalg.norm(r); u=np.cross(r,w); f=P/2/np.tan(np.radians(fov)/2)
def pr(p):
    p=np.asarray(p,float)-e; z=p@w; return (P/2+f*(p@r)/z,P/2-f*(p@u)/z)
F=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',26)
C,G,HOLD,FADE=map(int,sys.argv[8:12]); S=C//G; c=[]
import os
sm=float(open(D+'/smax').read()) if os.path.exists(D+'/smax') else None
def cut(t): # polygon where x+y+z=t meets the cube
    q=[]
    for a in range(3):
        for b in itertools.product((0,1),repeat=2):
            x=t-sum(b)
            if 0<=x<=1: p=list(b); p.insert(a,x); q.append(p)
    q=np.array(q); m=q-q.mean(0); o=np.argsort(np.arctan2(m@[1,1,-2]/6**.5,m@[1,-1,0]/2**.5)); return [*q[o],q[o][0]]
for s in range(S):
    c.append(np.zeros((P,P,3),'f4'))
    for g in range(G):
        a=np.fromfile(f'{D}/{s*G+g}',np.float64).reshape(n,n,3)
        c[-1]+=np.stack([a[:,:,k].reshape(P,2,P,2).sum((1,3)) for k in range(3)],-1).astype('f4')
t=np.sum(c,0); m=t.sum(2)>0; br=t.sum(2)>np.percentile(t.sum(2)[m],70)
wb=np.median(t[br],0); print('white balance',wb); assert wb.min()>0
wb=wb/wb.max()                        # colour balance only
L=np.concatenate([(x/wb).mean(2)[x.sum(2)>0] for x in c]); E=1.2/np.percentile(L[::7],75)
fr=[]
for s in range(S):
    v=1-np.exp(-E*c[s]/wb); im=Image.fromarray((255*v**(1/2.2)).astype('u1')); d=ImageDraw.Draw(im)
    for a,b in itertools.combinations(list(itertools.product((0,1),repeat=3)),2):
        if sum(x!=y for x,y in zip(a,b))!=1: continue
        ax=[x!=y for x,y in zip(a,b)].index(True)
        front=any((e[o]>1) if a[o] else (e[o]<0) for o in range(3) if o!=ax)
        q=[pr(np.array(a)+(np.array(b)-np.array(a))*k/80) for k in range(81)]
        for k in range(80):
            if front or k%4<2: d.line(q[k]+q[k+1],fill=(255,)*3 if front else (140,)*3,width=2 if front else 1)
    for x in (s/S,(s+1)/S):
        q=[pr(p) for p in (cut(min(2.9999,max(1e-4,x*sm))) if sm else ((x,0,0),(x,1,0),(x,1,1),(x,0,1),(x,0,0)))]
        for a,b in zip(q,q[1:]): d.line(a+b,fill=(80,140,230),width=2)
    for k,p in (('0',(-.04,)*3),('X',(1.06,0,0)),('Y',(0,1.06,0)),('Z',(0,0,1.06))):
        d.text(pr(p),k,fill='yellow',font=F,anchor='mm',stroke_width=2,stroke_fill='black')
    d.text((20,20),f'x+y+z = {s*sm/S:.2f}-{(s+1)*sm/S:.2f} (cube side = 1)' if sm else f'x = {100*s//S}-{100*(s+1)//S}%',fill='white',font=F)
    fr.append(np.asarray(im))
p=subprocess.Popen(['gst-launch-1.0','-q','fdsrc','fd=0','!','rawvideoparse','format=rgb',f'width={P}',f'height={P}','framerate=30/1','!',
  'videoconvert','!','openh264enc','bitrate=12000000','rate-control=bitrate','complexity=high','!','h264parse','!','mp4mux','faststart=true','!','filesink','location='+sys.argv[7]],stdin=subprocess.PIPE)
for s in range(S):
    for k in range(HOLD): p.stdin.write(fr[s].tobytes())
    if s<S-1:
        for k in range(1,FADE+1):
            t=k/(FADE+1); p.stdin.write((fr[s]*np.float32(1-t)+fr[s+1]*np.float32(t)).astype('u1').tobytes())
p.stdin.close(); sys.exit(p.wait())
