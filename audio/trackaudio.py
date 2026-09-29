# Render the audio of a captured trace segment (music + sfx events) to a per-tick PSG stream.
from psgconv import *
def parse_events(trace, t0, t1):
    ev=[]
    for l in open(trace):
        p=l.split()
        if len(p)<2 or not p[0].isdigit(): continue
        t=int(p[0])
        if t<t0 or t>t1: continue
        k=p[1]
        if k=='music': ev.append((t,'music',int(p[2]),int(p[3]),int(p[4])))
        elif k=='sfxplay': ev.append((t,'sfx',int(p[2][2:]),int(p[3][3:]),int(p[4][4:]),int(p[5][3:])))
        elif k=='sfxstop': ev.append((t,'sfxstop',int(p[2][2:])))
        elif k in('musstop','stopall'): ev.append((t,k))
    return ev
def render(ev, t0, t1, res, S):
    ntick=int((t1-t0)*60/1000)+1
    voices={}; seq=None; mus_next=None; mix=Mixer(); out=[]; ei=0
    for k in range(ntick):
        now=t0+k*1000/60
        while ei<len(ev) and ev[ei][0]<=now:
            e=ev[ei]; ei+=1; tt=(e[0]-t0)*60/1000
            if e[1]=='music':
                _,_,rn,delay,pos=e
                if rn:
                    seq=Sequencer(res,rn,delay,pos,S); mus_next=e[0]
                    for c in range(4): voices.pop(('m',c),None)
                elif delay and seq: seq.delay=delay
                else: seq=None
            elif e[1] in('musstop',):
                seq=None; [voices.pop(('m',c),None) for c in range(4)]
            elif e[1]=='stopall':
                seq=None; voices.clear()
            elif e[1]=='sfx':
                _,_,ch,rn,hz,vol=e
                if rn in S: voices[('s',ch)]=Voice(S[rn],hz,vol,tt)
            elif e[1]=='sfxstop': voices.pop(('s',e[2]),None)
        while seq and seq.playing and mus_next<=now:
            seq.handle((mus_next-t0)*60/1000,voices,lambda v:None)
            mus_next+=seq.tick_ms()
        rend={}
        for key,v in list(voices.items()):
            if v is None: continue
            r=v.render(k)
            if r is None: voices[key]=None
            else: rend[key]=r
        out.append(bytes(mix.tick(rend)))
    return out
def to_stream(ticks):
    b=bytearray()
    for t in ticks: b.append(len(t)); b+=t
    b.append(0xFF)
    return bytes(b)
def to_wav(ticks, fn, sr=44100):
    """reference SN76489 synthesis for listening/checking"""
    import wave
    per=[1]*3+[16]; att=[15]*4; latch=0; nmode=0; lfsr=0x8000; cnt=[0]*4; outv=[1]*4
    vol=[10**(-a*2/20) if a<15 else 0 for a in range(16)]
    samples=[]; step=PSGCLK/16/sr
    acc=[0.0]*4
    for t in ticks:
        for b in t:
            if b&0x80:
                latch=(b>>5)&3
                if b&0x10: att[latch]=b&15
                elif latch<3: per[latch]=(per[latch]&0x3F0)|(b&15)
                else: nmode=b&7; lfsr=0x8000
            else:
                if latch<3: per[latch]=(per[latch]&15)|((b&0x3F)<<4)
        n=int(sr/60)
        for i in range(n):
            s=0.0
            for c in range(3):
                acc[c]+=step
                p=max(per[c],1)
                while acc[c]>=p: acc[c]-=p; outv[c]=-outv[c]
                s+=outv[c]*vol[att[c]]
            np_=[16,32,64,per[2]*1][nmode&3] if (nmode&3)<3 else max(per[2],1)
            acc[3]+=step
            while acc[3]>=np_:
                acc[3]-=np_
                bit=((lfsr^(lfsr>>3))&1) if nmode&4 else (lfsr&1)
                lfsr=(lfsr>>1)|(bit<<15); outv[3]=1 if lfsr&1 else -1
            s+=outv[3]*vol[att[3]]
            samples.append(int(max(-32767,min(32767,s*6000))))
    w=wave.open(fn,'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
    import struct as st; w.writeframes(st.pack('<%dh'%len(samples),*samples)); w.close()
if __name__=='__main__':
    import sys
    res=load_resources(); S=samples_of(res)
    ev=parse_events('/home/claude/w/cap/intro.txt',0,164520)
    ticks=render(ev,0,164520,res,S)
    st=to_stream(ticks); open('/home/claude/w/audio/intro_psg.bin','wb').write(st)
    print('ticks',len(ticks),'bytes',len(st),'nonempty',sum(1 for t in ticks if t))
    to_wav(ticks[:60*40],'/tmp/intro40.wav')
