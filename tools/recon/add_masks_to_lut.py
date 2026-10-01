# Reconstructed build: copy the mask data (ROM bank 9) behind the coordinate LUTs in bank 10 (offset 0x1400), as mkgame.py now lays it out.
G='/home/claude/w/sms/gen/'; B=16384; g=bytearray(open(G+'game.bin','rb').read())
m=g[(9-2)*B:(9-2)*B+6096]; t=(10-2)*B+0x1400
assert set(g[t:t+len(m)])<={0} or g[t:t+len(m)]==m; g[t:t+len(m)]=m; open(G+'game.bin','wb').write(g)
print('masks copied to bank 10 @0x9400')
