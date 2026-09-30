# rank prefix states by (independent runs they appear in) x (polygons they save); write the top N keys to want.bin
import glob, struct, collections, sys
N=int(sys.argv[1])
runs=collections.Counter(); polys={}
for fn in glob.glob('/home/claude/w/cap/var/px_*.bin'):
    b=open(fn,'rb').read(); seen=set()
    for i in range(0,len(b)-7,8):
        k,p=struct.unpack('<II',b[i:i+8]); polys[k]=p
        if k not in seen: seen.add(k); runs[k]+=1
sb=open('/home/claude/w/cap/var/seen.bin','rb').read(); base=set(struct.unpack('<1903I',sb[:1903*4]))
cand=sorted(((runs[k]*polys[k],k) for k in runs if k not in base and runs[k]>=2), reverse=True)[:N]
open('/home/claude/w/cap/var/want.bin','wb').write(b''.join(struct.pack('<I',k) for v,k in cand))
print('candidates (>=2 runs, not in ROM): %d; chosen %d; value range %d..%d'%(sum(1 for k in runs if k not in base and runs[k]>=2),len(cand),cand[0][0],cand[-1][0]))
