# star_color.py counts.u32 PIXELS out.png: hue = mean distance from (0,0,0)
# (near = red, far = violet, spread evenly by rank), brightness = log density.
import sys, numpy as np, colorsys
from PIL import Image
n=int(sys.argv[2])
def load(f,t): return np.fromfile(f,t).reshape(n//2,2,n//2,2).sum((1,3),dtype=np.float64)
h=load(sys.argv[1],np.uint32); d=load(sys.argv[1]+'.dist',np.uint64)
m=h>0; r=d[m]/h[m]
print("points",h.sum(),"mean distance percentiles",np.percentile(r,[0,1,10,50,90,99,100]).round())
w=m&(h>=30)                       # rank only pixels with enough points
e=np.quantile(d[w]/h[w],np.linspace(0,1,257))
hue=np.interp(r,np.unique(e),np.linspace(0,.8,len(np.unique(e))))
g=np.log1p(h[m]); v=np.clip(g/np.percentile(g,99),0,1)**1.3
lut=np.array([colorsys.hsv_to_rgb(x,1,1) for x in np.linspace(0,.8,1024)])
c=lut[(hue/.8*1023).astype(int)]
img=np.zeros((n//2,n//2,3),'u1'); img[m]=(255*c*v[:,None]).astype('u1')
Image.fromarray(img).save(sys.argv[3])
