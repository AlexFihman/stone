# tri_video.py RAW PIXELS BINS WINDOW out.mp4: each frame adds WINDOW bins of stone_tri;
# the first part of the window is red, the middle green, the last blue.
import sys, subprocess, numpy as np
from PIL import Image, ImageDraw
P,F,W=map(int,sys.argv[2:5]); a=np.memmap(sys.argv[1],np.float32,'r',shape=(F,P,P))
t=(np.arange(W)+.5)/W; w=np.stack([np.maximum(0,1-2*t),1-abs(2*t-1),np.maximum(0,2*t-1)]); w/=w.sum(1,keepdims=True)
def frame(q): # light per point ~ 1/s keeps the brightness of the pattern constant
    b=np.asarray(a[q:q+W]); s=(np.arange(q,q+W)+.5)/F
    return np.stack([np.tensordot(w[k]/s,b,1) for k in range(3)],-1)
ref=np.concatenate([x[x>0] for x in (frame(int(F*z)).mean(2) for z in (.3,.45,.58))]); E=1.1/np.percentile(ref,70)
m=Image.new('L',(P,P)); d=ImageDraw.Draw(m); sc=.94*P/2**.5; h=6**-.5
q=[(P/2+sc*x,P/2-sc*(y-.5*h)) for x,y in ((-2**-.5,-h),(2**-.5,-h),(0,2*h))]
d.line(q+[q[0]],fill=70,width=2); m=np.asarray(m)[...,None]
p=subprocess.Popen(['gst-launch-1.0','-q','fdsrc','fd=0','!','rawvideoparse','format=rgb',f'width={P}',f'height={P}','framerate=30/1','!',
  'videoconvert','!','openh264enc','bitrate=16000000','rate-control=bitrate','complexity=high','!','h264parse','!','mp4mux','faststart=true','!','filesink','location='+sys.argv[5]],stdin=subprocess.PIPE)
for k in range(F-W+1):
    p.stdin.write(np.maximum((255*(1-np.exp(-E*frame(k)))**(1/2.2)).astype('u1'),m).tobytes())
p.stdin.close(); sys.exit(p.wait())
