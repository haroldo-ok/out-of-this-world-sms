# For the reconstructed build (const.bin taken from the shipped ROM): add the bbox index the way mkconst.py does.
import struct
G='/home/claude/w/sms/gen/'; c=bytearray(open(G+'const.bin','rb').read()); a=0x8613-0x8000; n=742
def bbox_index(keys):
    import bisect
    n=len(keys); s2=bisect.bisect_left(keys,(2,0))
    return [min(bisect.bisect_left(keys,(1,k<<6)),s2) for k in range(1016)]+[s2,n]
keys=[(c[a+12*i+2],c[a+12*i]|(c[a+12*i+1]<<8)) for i in range(n)]; assert keys==sorted(keys)
idx=bbox_index(keys); c[0x3000:0x3000+2*len(idx)]=struct.pack('<%dH'%len(idx),*idx); open(G+'const.bin','wb').write(c)
print('bbox index: %d entries, largest bucket %d'%(len(idx), max(idx[i+1]-idx[i] for i in range(len(idx)-1))))
