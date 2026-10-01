# For the reconstructed build (const.bin taken from the shipped ROM): add the bbox index the way mkconst.py now does.
import struct, bisect
G='/home/claude/w/sms/gen/'; c=bytearray(open(G+'const.bin','rb').read()); a=0x8613-0x8000; n=742
keys=[(c[a+12*i+2],c[a+12*i]|(c[a+12*i+1]<<8)) for i in range(n)]; assert keys==sorted(keys)
idx=[bisect.bisect_left(keys,(1 if k<256 else 2,(k&255)<<8)) for k in range(512)]+[n]
c[0x3000:0x3000+1026]=struct.pack('<513H',*idx); open(G+'const.bin','wb').write(c)
print('bbox index written; largest bucket', max(idx[i+1]-idx[i] for i in range(512)))
