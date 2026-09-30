import sys
fn=sys.argv[1]; s=open(fn).read()
a='  materialize(buf[1]);\n'
assert a in s
s=s.replace(a,a+'''#ifdef PAGETRACE
  { uint16_t c, sum = 0; uint8_t b, ref[32]; uint16_t *pc;
    materialize(buf[1]); pc = page[buf[1]];
    for (c = 0; c < NCELL; c++) { load_tile(pc[c], ref); for (b = 0; b < 32; b++) sum = (sum << 1 | sum >> 15) ^ ref[b]; }
    DBGTR2 = (uint8_t)sum; DBGTR2 = (uint8_t)(sum >> 8); }
#endif
''',1)
s=s.replace('__sfr __at 0xBE VDPD;\n','__sfr __at 0xBE VDPD;\n#ifdef PAGETRACE\n__sfr __at 0x04 DBGTR2;\n#endif\n',1)
open(fn,'w').write(s)
