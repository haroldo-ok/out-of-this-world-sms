#!/bin/bash
set -e
cd /home/claude/w/sms
DK=$HOME/.devkitsms
CF="$XCF -mz80 -I$DK/include --peep-file $DK/include/peep-rules.txt --opt-code-speed --max-allocs-per-node 5000"
mkdir -p obj
OBJS=""
for f in ${SRCS:-fmv main}; do sdcc -c $CF src/$f.c -o obj/$f.rel; OBJS="$OBJS obj/$f.rel"; done
sdcc -o obj/game.ihx -mz80 --no-std-crt0 --data-loc 0xC000 $DK/lib/crt0_sms.rel $OBJS $DK/lib/SMSlib.lib $DK/lib/PSGlib.lib
$DK/bin/ihx2sms obj/game.ihx obj/code.sms
python3 mkrom.py ootw.sms obj/code.sms $BLOBS
