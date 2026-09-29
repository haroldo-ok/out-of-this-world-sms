#!/usr/bin/env python3
# try.py inputs.txt nframes [sheet.png f1 f2 ...] -> screen timeline, death, part; optional contact sheet
import subprocess, sys, numpy as np, os
inp, n = os.path.abspath(sys.argv[1]), int(sys.argv[2])
env = dict(os.environ, OOTW_VARS='/home/claude/w/cap/vars_16002_start.bin', VARS='/tmp/tv.bin')
subprocess.run(['./oracle','../data','16002',str(n),'/tmp/tp.bin','/tmp/tpt.txt',inp], cwd='/home/claude/w/rawgl', env=env, capture_output=True)
v = np.fromfile('/tmp/tv.bin','<i2').reshape(-1,256)
out=[]; prev=None
for i,x in enumerate(v[:,0x67]):
    if x!=prev: out.append('%d:%d'%(i,x)); prev=x
dead=None; n2=0
for l in open('/tmp/tpt.txt'):
    p=l.split()
    if len(p)>1 and p[1]=='display': n2+=1
    if ' string 317 ' in l and dead is None: dead=n2
    if len(p)>2 and p[1]=='part' and p[2]!='16002': out.append('PART%s@%d'%(p[2],n2))
print('screens',' '.join(out),'| dead at display',dead,'| frames',len(v))
if len(sys.argv)>3:
    sys.path.insert(0,'/home/claude/w'); from fview import read
    from PIL import Image
    ids=[int(x) for x in sys.argv[4:]]; ims=[]
    for i in ids:
        t,pal,idx=read('/tmp/tp.bin',i); ims.append(pal[idx][::2,::2])
    Image.fromarray(np.concatenate(ims,1)).save(sys.argv[3])
