# Assemble the final ROM: code banks 0-1 + data blobs at fixed banks; pad to 4 MB; fix SEGA header.
import sys
BANK=16384
def main(out, code, *blobs):   # blobs: bank:path
    code=open(code,'rb').read()
    rom=bytearray(code[:2*BANK])
    rom+=b'\xff'*(2*BANK-len(rom))
    size=4*1024*1024
    rom+=b'\xff'*(size-len(rom))
    used=[0]*256; used[0]=used[1]=1
    # banked code linked at N*0x10000 + (0x4000..0x7FFF): read it from the .ihx (ihx2sms ignores it)
    import os
    ihx=os.path.join(os.path.dirname(os.path.abspath(sys.argv[2])),'game.ihx')
    base=0
    for line in open(ihx):
        line=line.strip()
        if not line.startswith(':'): continue
        n=int(line[1:3],16); a=int(line[3:7],16); t=int(line[7:9],16); d=bytes.fromhex(line[9:9+2*n])
        if t==4: base=int(line[9:13],16)<<16
        elif t==0 and base:
            A=base+a; k=A>>16; assert 0x4000<=(A&0xFFFF)<0x8000, 'banked code must be linked for slot 1: %x'%A
            o=k*BANK+(A&0x3FFF); rom[o:o+len(d)]=d; used[k]=1
    for spec in blobs:
        b,p=spec.split(':'); b=int(b); d=open(p,'rb').read()
        nb=(len(d)+BANK-1)//BANK
        for k in range(b,b+nb):
            assert not used[k],'bank %d overlap (%s)'%(k,p); used[k]=1
        assert b+nb<=256,'blob %s overflows 4MB'%p
        rom[b*BANK:b*BANK+len(d)]=d
    hdr=0x7FF0; assert rom[hdr:hdr+8]==b'TMR SEGA'
    csum=sum(rom[0:0x7FF0])&0xFFFF          # checksum range = 32K (size code 0xC)
    rom[hdr+10]=csum&255; rom[hdr+11]=csum>>8
    rom[hdr+15]=(rom[hdr+15]&0xF0)|0x0C
    open(out,'wb').write(rom)
    print('%s: 4096 KB, %d/256 banks used, checksum %04x'%(out,sum(used),csum))
if __name__=='__main__': main(*sys.argv[1:])
