import numpy as np
REC=4+48+64000
LV=np.array([0,0,0,1,1,1,1,1,2,2,2,2,2,3,3,3],np.uint8)   # 4-bit -> SMS 2-bit
def load_frames(fn, start=0, count=None):
    raw=np.memmap(fn,dtype=np.uint8,mode='r')
    n=len(raw)//REC
    if count is None: count=n-start
    rec=raw[:n*REC].reshape(n,REC)[start:start+count]
    t=rec[:,:4].copy().view('<u4').ravel()
    pal=rec[:,4:52].reshape(-1,16,3)>>4          # 4-bit per channel
    idx=rec[:,52:].reshape(-1,200,320)
    return t,pal,idx
XS=np.floor((np.arange(256)+0.5)*320/256).astype(int)
YS=np.floor((np.arange(192)+0.5)*200/192).astype(int)
def sms_pal(pal4):   # (16,3) 4-bit -> 16 SMS colour bytes
    l=LV[pal4]; return (l[:,0] | (l[:,1]<<2) | (l[:,2]<<4)).astype(np.uint8)
def downsample(idx): return idx[YS][:,XS]
