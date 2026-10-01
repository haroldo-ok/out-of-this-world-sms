#!/bin/bash
set -e
cd /home/claude/w/sms
DK=$HOME/.devkitsms
CF="$XCF -mz80 -I$DK/include --peep-file $DK/include/peep-rules.txt --opt-code-speed --max-allocs-per-node 5000"
mkdir -p obj
# main first so it (and the bank-switching around the FMV call) stays in slot 0;
# the FMV player is linked as slot-1 code in ROM bank 12 (mapped only while it runs)
OBJS="obj/main.rel"
sdcc -c $CF src/main.c -o obj/main.rel
for f in ${SRCS:-fmv title engine}; do
  [ "$f" = main ] && continue
  if [ "$f" = fmv ] || [ "$f" = title ]; then sdcc -c $CF --codeseg BANK12 --constseg BANK12 src/$f.c -o obj/$f.rel; else sdcc -c $CF src/$f.c -o obj/$f.rel; fi
  OBJS="$OBJS obj/$f.rel"
done
sdcc -o obj/game.ihx -mz80 --no-std-crt0 --data-loc 0xC000 -Wl-b_BANK12=0xC4000 $DK/lib/crt0_sms.rel $OBJS $DK/lib/SMSlib.lib $DK/lib/PSGlib.lib
python3 - <<'PY'
import re
m=open('obj/game.map').read(); end=0
for n in ('_CODE','_HOME','_INITIALIZER','_GSINIT','_GSFINAL'):
    r=re.search(r'^%s\s+([0-9A-F]{8})\s+([0-9A-F]{8})'%n,m,re.M)
    if r: end=max(end,int(r.group(1),16)+int(r.group(2),16))
print('fixed code ends at 0x%04X (%d bytes free before the header)'%(end,0x7FF0-end))
assert end<=0x7FF0,'fixed code overflows into the ROM header'
PY
$DK/bin/ihx2sms obj/game.ihx obj/code.sms
python3 mkrom.py ootw.sms obj/code.sms ${BLOBS:-2:gen/game.bin 13:gen/const.bin 64:gen/fmv.bin 150:gen/bg.bin}
