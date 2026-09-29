import numpy as np,sys
o=np.fromfile('/tmp/ov.bin','<i2').reshape(-1,256)
t=np.fromfile(sys.argv[1],'<i2'); t=t[:len(t)//256*256].reshape(-1,256)
n=min(len(o),len(t))
for k in range(n):
    if (o[k][:0xE0]!=t[k][:0xE0]).any():
        print('first mismatch at display',k,[(hex(i),int(o[k][i]),int(t[k][i])) for i in range(0xE0) if o[k][i]!=t[k][i]][:10]); break
else: print('all',n,'displays match the oracle')
