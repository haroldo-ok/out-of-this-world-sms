import re, base64, zlib, struct
js = open('wd/src/ootwdemo-data.js').read()
# types from rawgl static Amiga table (type is first field; consistent across versions)
src = open('rawgl/staticres.cpp').read()
m = re.search(r'_memListAmigaFR\[146\] = \{(.*?)\};', src, re.S)
types = [int(x.split(',')[0].strip('{ ')) for x in re.findall(r'\{[^}]*\}', m.group(1))]
assert len(types)==146
res = {}
sizes={int(n,16):int(s) for n,s in re.findall(r'export const size([0-9a-f]{2}) = (\d+)', js)}
for name,b64 in re.findall(r'export const data([0-9a-f]{2}) = "([^"]*)"', js):
    d=base64.b64decode(b64); i=int(name,16)
    res[i] = d if len(d)==sizes[i] else zlib.decompress(d)
    assert len(res[i])==sizes[i]
bank=bytearray(); ml=bytearray()
for i in range(146):
    if i in res:
        d=res[i]; pos=len(bank); bank+=d
        ml+=struct.pack('>BBIBBIII',0,types[i],0,0,1,pos,len(d),len(d))
    else:
        ml+=struct.pack('>BBIBBIII',0,types[i],0,0,0,0,0,0)
ml+=struct.pack('>BBIBBIII',0xff,0,0,0,0,0,0,0)
open('data/memlist.bin','wb').write(ml); open('data/bank01','wb').write(bank)
print(len(res),'resources', len(bank),'bytes')
for i in sorted(res): print('%02x t%d %d'%(i,types[i],len(res[i])),end=' | ')
