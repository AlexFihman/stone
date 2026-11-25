# zoom_video.py RAW PIXELS WINDOW S0 RATIO out.mp4: each frame is WINDOW bins of stone_zoom
# (a fixed percentage of s); first part of the window red, middle green, last blue.
# Also prints how alike successive, non-overlapping slices are at several sizes.
import sys, os, subprocess, numpy as np
from PIL import Image, ImageDraw, ImageFont
P,W=int(sys.argv[2]),int(sys.argv[3]); s0,R=float(sys.argv[4]),float(sys.argv[5]); HW=P//2
F=os.path.getsize(sys.argv[1])//(4*P*HW); a=np.memmap(sys.argv[1],np.float32,'r',shape=(F,P,HW))
t=(np.arange(W)+.5)/W; w=np.stack([np.maximum(0,1-2*t),1-abs(2*t-1),np.maximum(0,2*t-1)]); w/=w.sum(1,keepdims=True)
S=lambda q:s0*R**q
def blur(x,g):
    if g<.3: return x
    n=int(3*g)+1; k=np.exp(-.5*(np.arange(-n,n+1)/g)**2); k/=k.sum()
    for ax in (0,1):
        x=sum(k[i+n]*np.roll(x,i,ax) for i in range(-n,n+1))
    return x
def frame(q):
    b=np.asarray(a[q:q+W]); s=S(np.arange(q,q+W)+.5); x=np.stack([np.tensordot(w[k]/s**2,b,1) for k in range(3)],-1)
    g=.3*(.95*P/.874)/S(q+W/2)          # at small s the stones are many pixels apart
    x=np.concatenate([x[:,::-1],x],1)    # mirror
    return blur(x,min(4,g))
m=lambda q:np.asarray(a[q:q+W]).sum(0)
def corr(q,b):
    x,y=(m(z).reshape(P//b,b,HW//b,b).sum((1,3)).ravel() for z in (q,q+W)); k=(x+y)>0
    return np.corrcoef(x[k],y[k])[0,1], x.sum()
print('size s, points/slice, correlation of neighbouring slices at 1, 4, 12 px')
for z in (250,1000,2000,4000,8000,16000,32000,64000,128000,245000):
    q=min(F-2*W,int(np.log(z/s0)/np.log(R))); r=[corr(q,b) for b in (1,4,12)]
    print(int(S(q)),int(r[0][1]),*(f'{x[0]:.3f}' for x in r),flush=True)
ref=np.concatenate([x[x>0] for x in (frame(int(F*z)).mean(2) for z in (.6,.8,.95))]); E=1.1/np.percentile(ref,70)
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',22)
p=subprocess.Popen(['gst-launch-1.0','-q','fdsrc','fd=0','!','rawvideoparse','format=rgb',f'width={P}',f'height={P}','framerate=30/1','!',
  'videoconvert','!','openh264enc','bitrate=16000000','rate-control=bitrate','complexity=high','!','h264parse','!','mp4mux','faststart=true','!','filesink','location='+sys.argv[6]],stdin=subprocess.PIPE)
for q in range(F-W+1):
    im=Image.fromarray((255*(1-np.exp(-E*frame(q)))**(1/2.2)).astype('u1'))
    ImageDraw.Draw(im).text((16,12),f'x+y+z = {S(q+W/2):,.0f}',fill=(150,150,150),font=font)
    p.stdin.write(im.tobytes())
p.stdin.close(); print('frames',F-W+1); sys.exit(p.wait())
