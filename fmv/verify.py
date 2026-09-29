# Reconstruct the picture from emitted records only (VRAM slots + nametables) and compare with ideal frames.
import numpy as np, pickle, sys
from encode import orient, pix
def verify(fl, recs, tiles, report=True):
    vram=[None]*384; nt=[None,None]; front=0; errs=[]
    for (tick,pal,im),r in zip(fl,recs):
        for s,i in r['uploads']: vram[s]=pix(tiles[i])
        back=1-front
        m=list(nt[back]) if nt[back] is not None else [None]*768
        for c,e in r['nt']: m[c]=e
        nt[back]=m; front=back
        out=np.zeros((24,32,8,8),np.uint8)
        for c,(s,fl) in enumerate(m): out[c//32,c%32]=orient(vram[s],fl)
        out=out.transpose(0,2,1,3).reshape(192,256)
        rgb=np.array([[(c&3)*85,((c>>2)&3)*85,((c>>4)&3)*85] for c in pal])
        e=(rgb[out]!=rgb[im]).any(2).mean()
        errs.append(e)
    errs=np.array(errs)
    if report: print('pixel error: mean %.3f%% p99 %.2f%% max %.2f%%'%(errs.mean()*100,np.percentile(errs,99)*100,errs.max()*100))
    return errs
if __name__=='__main__':
    fl,recs,tiles=pickle.load(open(sys.argv[1],'rb'))
    e=verify(fl,recs,tiles)
    w=np.argsort(e)[-8:]; print('worst frames',w,e[w])
