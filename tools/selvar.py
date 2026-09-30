# select variant states seen in >= T independent runs into cap/pc_var_sel.bin
import glob, struct, pickle, sys
T=int(sys.argv[1]); runs=pickle.load(open('/tmp/runs.pkl','rb'))
sel={k for k,c in runs.items() if c>=T}; out=open('/home/claude/w/cap/pc_var_sel.bin','wb'); n=0; got=set()
for fn in sorted(glob.glob('/home/claude/w/cap/var/pc_v*.bin')):
    b=open(fn,'rb').read()
    for i in range(len(b)//64004):
        k=struct.unpack('<I',b[i*64004:i*64004+4])[0]
        if k in sel and k not in got: out.write(b[i*64004:(i+1)*64004]); got.add(k); n+=1
print('selected %d states (threshold %d runs)'%(n,T))
