# Reconstruct sms/gen/*.h from the shipped ROM (data banks are reused as-is), so the code can be
# rebuilt byte-identically and profiled with exact symbols. Deterministic tables are regenerated and
# cross-checked against the ROM bytes.
import struct, re, math, sys
W='/home/claude/w'; G=W+'/sms/gen/'; B=16384
rom=open(W+'/out-of-this-world.sms','rb').read()
src=open(W+'/rawgl/staticres.cpp').read()
def u16(a,n): return list(struct.unpack('<%dH'%n,rom[a:a+2*n]))
def s16(a,n): return list(struct.unpack('<%dh'%n,rom[a:a+2*n]))
H=['/* reconstructed from the shipped ROM by tools/recon/recon_headers.py */','#include <stdint.h>']
H+=['#define BC_BANK 2','#define V1_BANK 4','#define V2_BANK 8','#define BC_SIZE 19731',
    '/* pal_sms moved to ROM bank 13 (constdata.h) */']
# initial vars: idx table right after the code that precedes it (found at 0x02ED)
A=0x02EF
n=21; idx=list(rom[A:A+n]); val=s16(A+n,n)
H+=['#define NINITVARS %d'%n,'static const uint8_t initvar_idx[]={%s};'%','.join(map(str,idx)),
    'static const int16_t initvar_val[]={%s};'%','.join(map(str,val))]
H.append('/* font8 moved to ROM bank 13 (constdata.h) */')
# strings (ids from the part's bytecode; texts from rawgl)
strs={}
for tab in ('Eng','Demo'):
    m=re.search(r'const StrEntry Video::_stringsTable%s\[\] = \{(.*?)\n\};'%tab,src,re.S)
    for sid,st in re.findall(r'\{\s*(0x[0-9A-Fa-f]+|\d+),\s*"((?:[^"\\]|\\.)*)"\s*\}',m.group(1)): strs.setdefault(int(sid,0),st)
used=sorted(set(int(x,16) for x in re.findall(r'drawString\(str=0x([0-9A-F]+)',open(W+'/cy/ootwdemo_16002.bytecode.txt').read())))
a=A+3*n; assert u16(a,len(used))==used, (u16(a,len(used)),used)
H+=['#define NSTRINGS %d'%len(used),'static const uint16_t str_id[]={%s};'%','.join(map(str,used)),
    'static const char * const str_txt[]={%s};'%','.join('"%s"'%strs[i] for i in used)]
# masks
mo=re.search(r'const uint16_t Graphics::_shapesMaskOffset\[\] = \{(.*?)\};',src,re.S)
md=re.search(r'const uint8_t Graphics::_shapesMaskData\[\] = \{(.*?)\};',src,re.S)
offs=[int(x,0) for x in re.findall(r'0x[0-9A-Fa-f]+|\d+',mo.group(1))]
data=bytes(int(x,0) for x in re.findall(r'0x[0-9A-Fa-f]+|\d+',md.group(1)))
moffs=[]; L=0
for o in offs:
    w,h=data[o],data[o+1]; moffs.append(L); L+=2+2*h
a+=4*len(used); assert u16(a,len(moffs))==moffs, 'mask_off mismatch'
H+=['#define MASK_BANK 10','#define MASK_ADDR 0x9400','#define NMASKS %d'%len(moffs),'static const uint16_t mask_off[]={%s};'%','.join(map(str,moffs))]
H+=['#define LUTX_MIN -512\n#define LUTX_MAX 1023\n#define LUTY_MIN -384\n#define LUTY_MAX 639','#define LUT_BANK 10','#define LUTY_OFF 3072']
recip=[0x4000//max(d,1) for d in range(256)]; a+=2*len(moffs); assert a==0x4d0 and u16(a,256)==recip
H.append('static const uint16_t recip[256]={%s};'%','.join(map(str,recip)))
volatt=[15 if v==0 else min(15,int(round(-20*math.log10(v/63)/2))) for v in range(64)]
assert list(rom[0x6d0:0x710])==volatt
H.append('static const uint8_t volatt[64]={%s};'%','.join(map(str,volatt)))
H+=['#define SFX_BANK 11','#define NSFX 55','/* sfx_tab moved to ROM bank 13 (constdata.h) */','#define GAME_BANK_LAST 11',
    '#define NBBOX 742','/* bbox_tab moved to ROM bank 13 (constdata.h) */']
open(G+'gamedata.h','w').write('\n'.join(H)+'\n')
open(G+'constdata.h','w').write('\n'.join(['/* reconstructed */','#define CONST_BANK 13',
 '#define PAL_SMS ((const uint8_t *)(0x8000 + 0))','#define PAL_SMS_ADDR 0x8000',
 '#define FONT8 ((const uint8_t *)(0x8000 + 512))','#define FONT8_ADDR 0x8200',
 '#define SFX_TAB ((const uint8_t *)(0x8000 + 1280))','#define SFX_TAB_ADDR 0x8500',
 '#define BBOX_TAB ((const uint8_t *)(0x8000 + 1555))','#define BBOX_TAB_ADDR 0x8613',
 '#define DEMO_IN ((const uint16_t *)(0x8000 + 10459))','#define DEMO_IN_ADDR 0xA8DB','#define BBOX_IDX_ADDR 0xB000'])+'\n')
open(G+'demo_input.h','w').write('#define NDEMO 231\n/* demo_in moved to ROM bank 13 (constdata.h) */\n')
fb=rom[243*B:244*B]
open(G+'bgdata.h','w').write('/* reconstructed */\n#define NBG 5403\n#define BGT_BANK0 150\n#define BGR_BANK0 177\n#define BGM_BANK0 221\n#define BGF_BANK 243\n#define BGK_BANK 244\n')
t=rom[0x211:0x211+12]
open(G+'fmv_data.h','w').write('/* reconstructed */\n#define FMV_NCUTS 2\n#define FMV_INTRO 0\n#define FMV_CAPTURE 1\n'
 'static const unsigned char fmv_tab[][6]={\n  {%d,0x%02X,0x%02X,%d,0x%02X,0x%02X},\n  {%d,0x%02X,0x%02X,%d,0x%02X,0x%02X},\n};\n'%tuple(t)+
 '#define FMV_BANK_FIRST 64\n#define FMV_BANK_LAST 147\n')
print('headers written')
