# Full-screen FMV encoder: shared tile dictionary; per frame: tile uploads into free VRAM slots
# (never a slot visible on screen) + delta to the back nametable (double-buffered, flipped in VBlank).
# Tiles that don't fit (slots or time budget) are replaced by the nearest resident tile; later frames converge.
import numpy as np, sys, struct, collections
from common import *

NSLOT=384            # tiles 0..383; nametables at 0x3000 / 0x3800
CYC_TILE=1150; CYC_NT=62; CYC_RUN=420; CYC_FIX=5000; CYC_FRAME=59736
SLACK=0              # ticks a frame may be shown late before uploads get capped

def orient(a,fl):
    if fl&1: a=a[:,::-1]
    if fl&2: a=a[::-1]
    return a

class Dict:
    def __init__(s): s.ids={}; s.tiles=[]
    def get(s,key):
        i=s.ids.get(key)
        if i is None: i=len(s.tiles); s.ids[key]=i; s.tiles.append(key)
        return i

def canon_frame(im):
    tl=im.reshape(24,8,32,8).transpose(0,2,1,3).reshape(768,8,8)
    out=[]
    for a in tl:
        v=[(orient(a,f).tobytes(),f) for f in range(4)]
        out.append(min(v,key=lambda x:x[0]))
    return out

def pix(key): return np.frombuffer(key,np.uint8).reshape(8,8)

def fit(cells, rgb, resident_keys, allowed):
    """cells: 768 (key,fl). Keys not in resident_keys cost a slot. Merge until #new <= allowed.
       Merge target may be any resident key or a kept new key, with any flip."""
    cnt=collections.Counter(k for k,_ in cells)
    new=[k for k in cnt if k not in resident_keys]
    if len(new)<=allowed: return cells,0
    if not resident_keys: allowed=max(allowed,1)
    res=[k for k in cnt if k in resident_keys]      # resident keys used by this frame
    pool=list(dict.fromkeys(res+list(resident_keys)))
    # candidate arrays: pool keys x 4 flips
    def colarr(keys):
        a=np.array([[orient(pix(k),f) for f in range(4)] for k in keys])  # n x4 x8x8
        return rgb[a].astype(np.int32)                                    # n x4x8x8x3
    newc=rgb[np.array([pix(k) for k in new])].astype(np.int32)          # m x8x8x3
    poolc=colarr(pool) if pool else np.zeros((0,4,8,8,3),np.int32)
    newflip=colarr(new)
    m=len(new)
    # distance new_i -> pool_j (best flip), new_i -> new_j (best flip)
    def dist(A,B):  # A: m x8x8x3 ; B: n x4x8x8x3  -> m x n best, flip
        out=np.empty((len(A),len(B)),np.int64); fo=np.empty((len(A),len(B)),np.int8)
        for i in range(len(A)):
            d=((B-A[i])**2).sum(axis=(2,3,4)); out[i]=d.min(1); fo[i]=d.argmin(1)
        return out,fo
    Dp,Fp=dist(newc,poolc) if len(pool) else (np.full((m,0),1<<60),np.zeros((m,0),np.int8))
    Dn,Fn=dist(newc,newflip); np.fill_diagonal(Dn,1<<60)
    w=np.array([cnt[k] for k in new],np.int64)
    alive=np.ones(m,bool); target={}
    bestp=Dp.min(1) if Dp.shape[1] else np.full(m,1<<60)
    removed=0
    while alive.sum()>allowed:
        Dn_alive=np.where(alive[None,:],Dn,1<<60)
        bestn=Dn_alive.min(1)
        best=np.minimum(bestp,bestn)
        cost=np.where(alive,best*w,1<<62)
        i=int(cost.argmin())
        if best[i]>=(1<<60): break
        if bestp[i]<=bestn[i]:
            j=int(Dp[i].argmin()); target[new[i]]=(pool[j],int(Fp[i,j]))
        else:
            j=int(Dn_alive[i].argmin()); target[new[i]]=(new[j],int(Fn[i,j])); w[j]+=w[i]
        alive[i]=False; removed+=1
    def resolve(k,fl):
        # cell shows orient(pix(k),fl) ; k ~ orient(pix(t),tf) ; so cell ~ orient(orient(pix(t),tf),fl) = orient(pix(t), tf^fl)
        while k in target:
            t,tf=target[k]; k=t; fl=fl^tf
        return k,fl
    return [resolve(k,fl) for k,fl in cells],removed

def encode(frames_list, dic, stats):
    slots=[None]*NSLOT; lastuse=[-1]*NSLOT; where={}        # where: key->slot
    nt=[[None]*768,[None]*768]; front=0
    recs=[]; prev_pal=None; shown=set(); t_ready=0; late=[]
    for fi,(tick,pal,im) in enumerate(frames_list):
        rgb=np.array([[(c&3)*85,((c>>2)&3)*85,((c>>4)&3)*85] for c in pal],np.int32)
        cells=canon_frame(im)
        distinct=set(k for k,_ in cells)
        # slots we may overwrite: not on screen and not holding a key this frame wants
        protect=set(where[k] for k in distinct if k in where)
        free=[s for s in range(NSLOT) if s not in shown and s not in protect]
        # time budget
        sched=tick*CYC_FRAME
        back=1-front
        est_nt=sum(1 for c in range(768) if nt[back][c] is None or nt[back][c][1]!=cells[c][1] or nt[back][c][0]!=cells[c][0])
        est_runs=min(40,est_nt)
        budget=(sched+SLACK*CYC_FRAME - t_ready - CYC_FIX - est_nt*CYC_NT - est_runs*CYC_RUN)//CYC_TILE
        allowed=max(0,min(len(free),budget))
        if fi==0: allowed=len(free)
        fs=set(free)
        safe=set(k for k,sl in where.items() if sl not in fs)
        cells,rem=fit(cells,rgb,safe,allowed)
        stats['substituted']+=rem; stats['subframes']+=(rem>0)
        distinct=list(dict.fromkeys(k for k,_ in cells))
        missing=[k for k in distinct if k not in where]
        assert len(missing)<=len(free)
        dset=set(distinct)
        free=[s for s in free if slots[s] not in dset]
        free.sort(key=lambda s:lastuse[s])
        uploads=[]
        for k,s in zip(missing,free):
            if slots[s] is not None: del where[slots[s]]
            slots[s]=k; where[k]=s; uploads.append((s,dic.get(k)))
        for k in distinct: lastuse[where[k]]=fi
        newmap=[(where[k],fl) for k,fl in cells]
        delta=[c for c in range(768) if nt[back][c]!=newmap[c]]
        merged=[]
        for c in delta:
            if merged and 0<c-merged[-1]<=5:
                merged.extend(range(merged[-1]+1,c+1))
            else: merged.append(c)
        delta=merged
        nruns=sum(1 for i,c in enumerate(delta) if i==0 or delta[i-1]!=c-1)
        nt[back]=newmap; front=back
        shown=set(where[k] for k in distinct)
        palchg=prev_pal is None or not np.array_equal(pal,prev_pal); prev_pal=pal
        recs.append(dict(tick=tick,pal=pal if palchg else None,uploads=uploads,nt=[(c,newmap[c]) for c in delta]))
        cost=len(uploads)*CYC_TILE+len(delta)*CYC_NT+nruns*CYC_RUN+CYC_FIX
        t_ready=max(t_ready+cost,sched)
        late.append((t_ready-sched)/CYC_FRAME)
        stats['uploads']+=len(uploads); stats['nt']+=len(delta)
    stats['late']=late
    return recs

def prep(fn, start, count):
    t,pal,idx=load_frames(fn,start,count)
    fl=[]; prev=None
    for f in range(len(t)):
        sp=sms_pal(pal[f]); canon=np.array([list(sp).index(c) for c in sp],np.uint8)
        im=canon[downsample(idx[f])]
        tick=int((int(t[f])-int(t[0]))*60//1000)
        if prev is not None and np.array_equal(im,prev[2]) and np.array_equal(sp,prev[1]): continue
        prev=(tick,sp,im); fl.append(prev)
    return fl

if __name__=='__main__':
    fl=prep('../cap/intro.bin',0,2982)
    print('frames after dedup',len(fl))
    dic=Dict(); st=collections.Counter()
    recs=encode(fl,dic,st)
    late=np.array(st.pop('late'))
    print(dict(st),'dict',len(dic.tiles))
    print('late ticks: mean %.2f max %.1f  frames late>2: %d'%(late.mean(),late.max(),(late>2).sum()))
    import pickle; pickle.dump((fl,recs,dic.tiles),open('/tmp/intro_enc.pkl','wb'))
