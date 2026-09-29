import bisect,sys
syms=sorted((int(l.split()[2],16),l.split()[1]) for l in open('/home/claude/w/sms/obj/game.noi') if l.startswith('DEF') and int(l.split()[2],16)<0x8000 and not l.split()[1].startswith(('l__','s__')))
addrs=[s[0] for s in syms]; agg={}; rows=[]
for l in open(sys.argv[1]):
    p=l.split()
    try: rows.append((int(p[1]),int(p[0],16)))
    except: pass
tot=sum(c for c,_ in rows); nf=int(sys.argv[2])
for c,a in rows:
    i=bisect.bisect_right(addrs,a)-1; agg[syms[i][1]]=agg.get(syms[i][1],0)+c
print('cycles/frame',tot//nf)
for k,v in sorted(agg.items(),key=lambda x:-x[1])[:22]: print('%5.1f%% %8d %s'%(100*v/tot,v//nf,k))
