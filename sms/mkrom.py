# Assemble the final ROM: code banks 0-1 + data blobs at fixed banks; pad to 4 MB; fix SEGA header.
import sys
BANK=16384
def main(out, code, *blobs):   # blobs: bank:path
    rom=bytearray(open(code,'rb').read())
    assert len(rom)<=2*BANK, 'code exceeds 32 KB: %d'%len(rom)
    rom+=b'\xff'*(2*BANK-len(rom))
    size=4*1024*1024
    rom+=b'\xff'*(size-len(rom))
    used=[0]*256; used[0]=used[1]=1
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
