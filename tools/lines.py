import bisect,sys
fn=sys.argv[2]
syms=sorted((int(l.split()[2],16),l.split()[1]) for l in open('/home/claude/w/sms/obj/game.noi') if l.startswith('DEF') and int(l.split()[2],16)<0x8000 and not l.split()[1].startswith(('l__','s__')))
addrs=[s[0] for s in syms]; rows=[]
for l in open(sys.argv[1]):
    p=l.split()
    try: rows.append((int(p[1]),int(p[0],16)))
    except: pass
tot=sum(c for c,_ in rows)
lst=open('/home/claude/w/sms/obj/engine.lst').read().split('\n')
i0=[i for i,l in enumerate(lst) if l.rstrip().endswith(fn+'::')][0]
base=int(lst[i0].split()[0],16); m=[]; last=''
for l in lst[i0:i0+30000]:
    if l.rstrip().endswith('::') and not l.rstrip().endswith(fn+'::'): break
    if 'engine.c:' in l: last=l.split('engine.c:')[1][:100]
    p=l.split()
    if p and len(p[0])==6:
        try: m.append((int(p[0],16)-base,last))
        except: pass
a0=[a for a,n in syms if n==fn][0]; agg={}
for c,a in rows:
    i=bisect.bisect_right(addrs,a)-1
    if syms[i][1]==fn:
        off=a-a0; cand=[s for o,s in m if o<=off]
        if cand: agg[cand[-1]]=agg.get(cand[-1],0)+c
for k,v in sorted(agg.items(),key=lambda x:-x[1])[:12]: print('%5.2f%% %s'%(100*v/tot,k))
