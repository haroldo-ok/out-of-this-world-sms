#!/usr/bin/env python3
# Random play variants for widening the page-state cache: either the recorded route up to a random frame and then
# random play, or random play from the start of part 16002. Writes oracle input files (frame mask-hex) + run lengths.
import random, sys
out, n, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
random.seed(seed)
route=[(int(f),int(m,16)) for f,m in (l.split() for l in open('/home/claude/w/cap/solution_16002.txt') if l.strip())]
MASKS=[0,0,1,2,1,2,8,4,0x80,0x81,0x82,0x81,0x82,0x09,0x0A,0x89,0x8A]
runs=[]
for k in range(n):
    if k % 3 == 2: start=0; pre=[]
    else:
        start=random.randint(0,740); pre=[(f,m) for f,m in route if f<=start]
    length=random.randint(120,320); t=start+1; seg=list(pre)
    while t<start+length:
        seg.append((t,random.choice(MASKS))); t+=random.randint(3,30)
    fn='%s/v%03d.txt'%(out,k)
    open(fn,'w').write(''.join('%d %x\n'%(f,m) for f,m in seg))
    runs.append((fn,start+length))
open('%s/runs.txt'%out,'w').write(''.join('%s %d\n'%r for r in runs))
print('%d variants'%n)
