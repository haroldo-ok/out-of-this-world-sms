# Encode all cutscenes against ONE shared tile dictionary, render their PSG audio, pack into ROM banks.
import sys, pickle, collections, numpy as np
sys.path.insert(0,'/home/claude/w/fmv'); sys.path.insert(0,'/home/claude/w/audio')
from common import load_frames
from encode import prep, encode, Dict
from verify import verify
from pack import build
from trackaudio import parse_events, render, to_stream
from psgconv import load_resources, samples_of
CUTS=[l.split() for l in open('/home/claude/w/fmv/cuts.txt') if l.strip() and not l.startswith('#')]
res=load_resources(); S=samples_of(res)
dic=Dict(); out=[]
for name,binf,tracef,f0,f1 in CUTS:
    f0=int(f0); f1=int(f1)
    t,_,_=load_frames(binf,f0,f1-f0+1)     # f1 = first frame NOT shown (hand-over frame)
    t0,tend=int(t[0]),int(t[-1])
    fl=prep(binf,f0,f1-f0)
    st=collections.Counter(); recs=encode(fl,dic,st); late=np.array(st.pop('late'))
    e=verify(fl,recs,dic.tiles,report=False)
    print('%s: %d frames, %d uploads, subst %d, late max %.2f, err mean %.3f%% max %.2f%%'%(name,len(fl),st['uploads'],st['substituted'],late.max(),e.mean()*100,e.max()*100))
    ev=parse_events(tracef,t0,tend); ticks=render(ev,t0,tend,res,S); audio=to_stream(ticks)
    end_tick=int((tend-t0)*60//1000)
    out.append((name,recs,audio,end_tick))
    pickle.dump((fl,recs,dic.tiles),open('/tmp/enc_%s.pkl'%name,'wb'))
print('shared dictionary: %d tiles'%len(dic.tiles))
build(out,dic.tiles,64,'/home/claude/w/sms/gen/fmv.bin','/home/claude/w/sms/gen/fmv_data.h')
