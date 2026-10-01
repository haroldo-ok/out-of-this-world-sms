# Relative bounding boxes (zoom 64) of every shape the part-16002 script can draw, for the overlap test
# around Lester (replaces walking the shapes at run time). Mirrors the engine's bbm walk exactly:
#   polygon: anchor +- (bbw>>1, bbh>>1); hierarchy: children at anchor - (hx,hy) + (cx,cy); mask: +-32.
import re, struct, sys
sys.path.insert(0,'/home/claude/w/audio')
from psgconv import load_resources
res=load_resources()
V1=res[0x1C][1]; V2=res[0x11][1]
def walk(data, off, x, y, box):
    i=data[off]; off+=1
    if i>=0xC0:
        w=data[off]>>1; h=data[off+1]>>1
        box[0]=min(box[0],x-w); box[1]=min(box[1],y-h); box[2]=max(box[2],x+w); box[3]=max(box[3],y+h)
    elif (i&0x3F)==2:
        ptx=x-data[off]; pty=y-data[off+1]; n=data[off+2]; off+=3
        # n is read as a byte into an int16 loop "for (; n >= 0; --n)": n+1 children
        for _ in range(n+1):
            o=(data[off]<<8)|data[off+1]; pox=ptx+data[off+2]; poy=pty+data[off+3]; off+=4
            if o&0x8000:
                color=data[off]; off+=2
                if color&0x80:
                    box[0]=min(box[0],pox-32); box[1]=min(box[1],poy-32); box[2]=max(box[2],pox+32); box[3]=max(box[3],poy+32)
                    continue
            walk(data, (o<<1)&0xFFFF, pox, poy, box)
dis=open('/home/claude/w/wd/ootwdemo_16002.bytecode.txt').read()
keys=sorted(set((1 if b=='1' else 2,int(o,16)) for o,b in re.findall(r'offset=0x([0-9a-f]+) \(bank([12])\.mat\)',dis)))
tab=bytearray(); n=0; floats=[]
for seg,off in keys:
    data=V1 if seg==1 else V2
    if off>=len(data): continue
    box=[32000,32000,-32000,-32000]
    walk(data,off,0,0,box)
    fl=1 if (box[0]<=box[2] and box[2]-box[0]<=4 and box[3]-box[1]<=4) else 0   # floating: kept out of the page hash
    tab+=struct.pack('<HBBhhhh',off,seg,fl,*box); n+=1
    if fl: floats.append((seg,off))
h=open('/home/claude/w/sms/gen/gamedata.h').read()
h=re.sub(r'\n#define NBBOX.*?\n(static const uint8_t bbox_tab\[\]=\{[^}]*\};\n)?','\n',h,flags=re.S)
h+='#define NBBOX %d\nstatic const uint8_t bbox_tab[]={%s};\n'%(n,','.join(str(b) for b in tab))
open('/home/claude/w/sms/gen/gamedata.h','w').write(h)
open('/home/claude/w/cap/floatset.bin','wb').write(b''.join(struct.pack('<BH',s_,o_) for s_,o_ in floats))
print('bbox table: %d shapes, %d bytes, %d floating'%(n,len(tab),len(floats)))
