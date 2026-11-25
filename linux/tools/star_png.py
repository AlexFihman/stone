# star_png.py in.u32 PIXELS out.png: 2x2 box downsample, log brightness
import sys, numpy as np
from PIL import Image
n=int(sys.argv[2]); h=np.fromfile(sys.argv[1],np.uint32).reshape(n,n)
h=h.reshape(n//2,2,n//2,2).sum((1,3),dtype=np.float64)
print("points",h.sum(),"max",h.max(),"percentiles of lit",np.percentile(h[h>0],[10,50,90,99]))
g=np.log1p(h).astype('f4'); g=np.clip((g-1)/(np.percentile(g[h>0],99.9)-1),0,1)
Image.fromarray((255*np.stack([np.clip(g*2,0,1),np.clip(g*1.6-.3,0,1),np.clip(g*2-1,0,1)],-1)).astype('u1')).save(sys.argv[3])
