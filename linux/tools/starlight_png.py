# starlight_png.py in PIXELS out.png: 2x2 downsample, white balance on the
# bright pixels, film exposure 1-exp(-E*light) per channel (dense places go white).
import sys, numpy as np
from PIL import Image
n=int(sys.argv[2]); c=np.memmap(sys.argv[1],np.float64,'r',shape=(n,n,3))
c=np.stack([c[:,:,k].reshape(n//2,2,n//2,2).sum((1,3)) for k in range(3)],-1)
L=c.sum(2); m=L>0
print('raw light R,G,B',c.sum((0,1)),'luminance pct',np.percentile(L[m],[1,10,50,90,99,100]))
br=L>np.percentile(L[m],70)
wb=np.median(c[br],0)
if len(sys.argv)>5: # 'each': balance each channel on its own lit pixels (thin slices)
    wb=np.array([np.percentile(c[...,k][c[...,k]>0],75) for k in range(3)])
assert wb.min()>0,"a typical bright pixel lacks a channel; use 'each'"
print('white balance',wb); c/=wb
E=float(sys.argv[4]) if len(sys.argv)>4 else 1.5   # exposure: 1 = typical bright pixel
v=1-np.exp(-E*c)
print('bright pixel mean colour',v[br].mean(0),'near-white share of lit',(v[m].min(1)>.8).mean())
Image.fromarray((255*v**(1/2.2)).astype('u1')).save(sys.argv[3])
