# height_png.py table.bin N PIXELS outprefix: f(x,y) seen from above, colour = height.
# Colours of the cells in a pixel are averaged; (0,0) is bottom left.
import sys, numpy as np, colorsys
from PIL import Image
f,n,P,o=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),sys.argv[4]
T=np.memmap(f,np.int32,'r',shape=(n,n))
L=np.array([colorsys.hsv_to_rgb(.72*(1-t),.95,.25+.75*min(1,1.6*t+.2)) for t in np.linspace(0,1,2048)],'f4') # blue low, red high
big=float(2*n)
tone={'lin':lambda k,x,y:k/(2.*n),
      'log':lambda k,x,y:np.log(np.maximum(k,1))/np.log(2.*n),
      'ratio':lambda k,x,y:np.log2(np.maximum(k,.25)/np.maximum(np.maximum(x,y),1))/2.+.5} # log2(k/max(x,y)), -1..1
def edges(ax):
    if ax=='lin': e=(np.arange(P)*(n/P)).astype(int); return e,0
    return np.minimum(n-1,np.floor(np.exp(np.linspace(0,np.log(n),P,endpoint=False))+1e-9).astype(int)),1
for ax,t in (('lin','lin'),('lin','log'),('log','log'),('log','ratio'),('lin','ratio')):
    e,_=edges(ax); img=np.zeros((P,P,3),'f4'); hi=np.append(e[1:],n)
    cols=np.arange(n)[None,:]; cw=np.maximum(1,hi-e)[:,None]
    for r in range(P):
        a,b=e[r],max(e[r]+1,hi[r])
        if r and a==e[r-1] and hi[r]==hi[r-1]: img[r]=img[r-1]; continue
        acc=np.zeros((n,3),'f4')
        for s in range(a,b,64):
            q=np.arange(s,min(b,s+64)); v=np.clip(tone[t](T[q],cols,q[:,None]),0,1)
            acc+=L[(v*2047).astype(int)].sum(0)
        # numpy reduceat: an empty range gives the single cell
        img[r]=np.add.reduceat(acc,e,axis=0)/cw/(b-a)
    Image.fromarray((255*np.clip(img[::-1],0,1)).astype('u1')).save(f'{o}_{ax}axes_{t}.png'); print(ax,t,flush=True)
