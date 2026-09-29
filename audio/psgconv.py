# Amiga-sample audio (Another World SfxPlayer music + sound effects) -> SN76489 PSG register streams.
import numpy as np, struct, math
PAULA=7159092; PSGCLK=3579545
PERIOD_TABLE=[1076,1016,960,906,856,808,762,720,678,640,604,570,538,508,480,453,428,404,381,360,
              339,320,302,285,269,254,240,226,214,202,190,180,170,160,151,143,135,127,120,113]
def load_resources(d='/home/claude/w/data'):
    ml=open(d+'/memlist.bin','rb').read(); bank=open(d+'/bank01','rb').read(); res={}
    for i in range(146):
        st,ty,_,_,bk,pos,ps,us=struct.unpack('>BBIBBIII',ml[i*20:i*20+20])
        if bk: res[i]=(ty,bank[pos:pos+us])
    return res
class Sample:
    def __init__(s,raw):
        s.len=struct.unpack('>H',raw[0:2])[0]*2; s.looplen=struct.unpack('>H',raw[2:4])[0]*2
        d=np.frombuffer(raw[8:8+s.len+s.looplen],np.int8).astype(np.float64)
        s.data=d; s.n=len(d)
        s.looppos=s.len if s.looplen else 0
        # envelope: RMS in blocks of 64 samples
        B=64; nb=max(1,(len(d)+B-1)//B); pad=np.zeros(nb*B); pad[:len(d)]=d
        s.env=np.sqrt((pad.reshape(nb,B)**2).mean(1))/128.0*1.6
        s.env=np.minimum(s.env,1.0)
        # fundamental via autocorrelation on a steady window
        seg=d[min(len(d)//8,2000):][:4096] if len(d)>600 else d
        seg=seg-seg.mean()
        f0=None; noisy=1.0
        if len(seg)>64 and np.abs(seg).sum()>0:
            ac=np.correlate(seg,seg,'full')[len(seg)-1:]; ac/=ac[0]
            lo=8; hi=min(len(ac)-1,1500)
            if hi>lo:
                k=lo+int(np.argmax(ac[lo:hi])); peak=ac[k]
                # refine to first strong peak (avoid octave errors)
                for j in range(lo,k):
                    if ac[j]>0.85*peak and ac[j]>=ac[j-1] and ac[j]>=ac[j+1]: k=j; break
                f0=1.0/k; noisy=1.0-peak
        s.f0=f0; s.noisy=noisy
        zc=np.mean(np.abs(np.diff(np.sign(seg))))/2 if len(seg)>2 else 0.1
        s.zcr=zc
    def is_noise(s): return s.f0 is None or s.noisy>0.55
    def amp_at(s,pos):
        """pos in sample units (float), returns amplitude or None if ended"""
        if pos>=s.n:
            if not s.looplen: return None
            pos=s.looppos+((pos-s.looppos)%s.looplen)
        return s.env[min(int(pos)//64,len(s.env)-1)]
def att_of(a):
    if a<=0.004: return 15
    return int(max(0,min(15,round(-20*math.log10(a)/2))))
class Voice:
    """a sample playing at a rate; renders per tick"""
    def __init__(s,smp,rate,vol,t0):
        s.smp=smp; s.rate=rate; s.vol=vol; s.t0=t0
    def render(s,t):
        pos=(t-s.t0)*s.rate/60.0
        a=s.smp.amp_at(pos)
        if a is None: return None
        a*=s.vol/64.0
        if s.smp.is_noise():
            bright=s.smp.zcr*s.rate   # crossings per second
            mode=0 if bright>6000 else (1 if bright>2500 else 2)
            return ('n',mode,a)
        hz=s.smp.f0*s.rate
        N=int(round(PSGCLK/(32*hz))) if hz>0 else 1023
        while N>1023: N//=2           # octave-fold into range
        N=max(1,N)
        return ('t',N,a)
class Mixer:
    """allocates voices to PSG channels and emits register writes per tick"""
    def __init__(s):
        s.state=[None]*4  # last (N or mode, att) per psg channel
        s.owner=[None]*3
    def tick(s,voices):
        """voices: dict key->rendered ('t',N,a)/('n',mode,a). returns list of PSG bytes"""
        tones=[(k,v) for k,v in voices.items() if v and v[0]=='t' and v[2]>0.004]
        noises=[(k,v) for k,v in voices.items() if v and v[0]=='n' and v[2]>0.004]
        tones.sort(key=lambda kv:-kv[1][2])
        chosen=dict(tones[:3])
        # keep owners stable
        newown=[None]*3; rest=[k for k in chosen]
        for c in range(3):
            if s.owner[c] in chosen: newown[c]=s.owner[c]; rest.remove(s.owner[c])
        for c in range(3):
            if newown[c] is None and rest: newown[c]=rest.pop(0)
        s.owner=newown
        out=[]
        for c in range(3):
            k=newown[c]
            if k is None: want=(None,15)
            else: _,N,a=chosen[k]; want=(N,att_of(a))
            old=s.state[c]
            if want[0] is not None and (old is None or old[0]!=want[0]):
                N=want[0]; out+=[0x80|(c<<5)|(N&15),(N>>4)&0x3F]
            if old is None or old[1]!=want[1]: out.append(0x90|(c<<5)|want[1])
            s.state[c]=(want[0] if want[0] is not None else (old[0] if old else None),want[1])
        if noises:
            _,mode,a=max(noises,key=lambda kv:kv[1][2])[1]; want=(mode,att_of(a))
        else: want=(None,15)
        old=s.state[3]
        if want[0] is not None and (old is None or old[0]!=want[0]): out.append(0xE0|4|want[0])
        if old is None or old[1]!=want[1]: out.append(0xF0|want[1])
        s.state[3]=(want[0] if want[0] is not None else (old[0] if old else None),want[1])
        return out
class Sequencer:
    """faithful port of rawgl SfxPlayer event handling, producing Voice starts"""
    def __init__(s,res,modnum,delay,order,samples):
        m=res[modnum][1]; s.m=m; s.num_order=m[0x3F]; s.order_table=m[0x40:0xC0]; s.data=m[0xC0:]
        s.delay=delay if delay else struct.unpack('>H',m[0:2])[0]
        s.cur_order=order; s.cur_pos=0; s.playing=True
        s.ins=[]
        for i in range(15):
            rn,vol=struct.unpack('>HH',m[2+i*4:6+i*4])
            s.ins.append((samples.get(rn),vol) if rn else (None,0))
        s.chvol=[0]*4
    def tick_ms(s): return s.delay*60*1000/PAULA
    def handle(s,t,voices,sync_cb):
        order=s.order_table[s.cur_order]
        base=s.cur_pos+order*1024
        for ch in range(4):
            n1,n2=struct.unpack('>HH',s.data[base+ch*4:base+ch*4+4])
            smp=None; vol=0
            if n1!=0xFFFD:
                si=(n2&0xF000)>>12
                if si and s.ins[si-1][0] is not None:
                    smp,vol=s.ins[si-1]; eff=(n2&0x0F00)>>8
                    if eff==5: vol=min(0x3F,vol+(n2&0xFF))
                    elif eff==6: vol=max(0,vol-(n2&0xFF))
                    s.chvol[ch]=vol
                    if ('m',ch) in voices and voices[('m',ch)]: voices[('m',ch)].vol=vol
            if n1==0xFFFD: sync_cb(n2)
            elif n1==0xFFFE: voices[('m',ch)]=None
            elif n1!=0 and smp is not None:
                rate=PAULA/(n1*2)
                voices[('m',ch)]=Voice(smp,rate,vol,t)
        s.cur_pos+=16
        if s.cur_pos>=1024:
            s.cur_pos=0; s.cur_order+=1
            if s.cur_order==s.num_order: s.playing=False
def sfx_rate(freq): return PAULA/(PERIOD_TABLE[min(freq,39)]*2)
def samples_of(res): return {i:Sample(d) for i,(ty,d) in res.items() if ty==0}
