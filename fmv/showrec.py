import numpy as np, pickle, sys
from PIL import Image
from encode import orient, pix
fl,recs,tiles=pickle.load(open(sys.argv[1],'rb')); want=[int(x) for x in sys.argv[3:]]
vram=[None]*384; nt=[None,None]; front=0; outs=[]
for f,((tick,pal,im),r) in enumerate(zip(fl,recs)):
    for s,i in r['uploads']: vram[s]=pix(tiles[i])
    back=1-front; m=list(nt[back]) if nt[back] is not None else [None]*768
    for c,e in r['nt']: m[c]=e
    nt[back]=m; front=back
    if f in want:
        out=np.zeros((24,32,8,8),np.uint8)
        for c,(s,fl_) in enumerate(m): out[c//32,c%32]=orient(vram[s],fl_)
        out=out.transpose(0,2,1,3).reshape(192,256)
        rgb=np.array([[(c&3)*85,((c>>2)&3)*85,((c>>4)&3)*85] for c in pal],np.uint8)
        outs.append(np.concatenate([rgb[im],np.zeros((192,4,3),np.uint8),rgb[out]],1))
Image.fromarray(np.concatenate(outs,0)).save(sys.argv[2])
