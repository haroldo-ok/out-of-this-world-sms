#!/usr/bin/env python3
# trace.py inputs.txt nframes [every] -> per-frame summary: screen prog x y state63 | alive slugs (x) | death
import subprocess, sys, numpy as np, os
inp, n = os.path.abspath(sys.argv[1]), int(sys.argv[2]); ev = int(sys.argv[3]) if len(sys.argv) > 3 else 10
env = dict(os.environ, OOTW_VARS='/home/claude/w/cap/vars_16002_start.bin', VARS='/tmp/tv.bin')
subprocess.run(['./oracle','../data','16002',str(n),'/tmp/tp.bin','/tmp/tpt.txt',inp], cwd='/home/claude/w/rawgl', env=env, capture_output=True)
v = np.fromfile('/tmp/tv.bin','<i2').reshape(-1,256)
dead=None; n2=0
for l in open('/tmp/tpt.txt'):
    p=l.split()
    if len(p)>1 and p[1]=='display': n2+=1
    if ' string 317 ' in l and dead is None: dead=n2
prev=None
for i in range(len(v)):
    r=v[i]; sl=[(k,int(r[0x46+3*k]),int(r[0x47+3*k])) for k in range(8)]
    alive=[(k,x) for k,x,s in sl if s==183]
    key=(int(r[0x67]),int(r[0x2A]))
    if i%ev==0 or key!=prev:
        print('%4d scr%d prog%d x%4d y%4d st%3d | alive %s'%(i,r[0x67],r[0x2A],r[1],r[2],r[0x63],alive))
    prev=key
print('dead at display',dead)
