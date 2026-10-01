#!/bin/bash
# Full page-state recapture: recorded route + underwater variants + the most recurring states of 1200 random runs.
# Honors FLOATSET (tiny shapes kept out of the hash). Output: cap/pc_*.bin -> game/mkbg.py 150
set -e
W=/home/claude/w; R=$W/rawgl; O="OOTW_VARS=$W/cap/vars_16002_start.bin"
mkdir -p $W/cap/old_auto; mv $W/cap/pc_*.bin $W/cap/old_auto/ 2>/dev/null || true
rm -f /tmp/rc_seen.bin
cd $R
env OOTW_VARS=$W/cap/vars_16002_start.bin BGSEEN=/tmp/rc_seen.bin BGCAP=$W/cap/pc_route.bin ./oracle ../data 16002 764 - /dev/null $W/cap/solution_16002.txt >/dev/null 2>&1
for t in none 0 30 32 34 36 38 40 44 48; do
  if [ $t = none ]; then printf "0 0\n" > /tmp/iu.txt; else printf "0 0\n$t 8\n" > /tmp/iu.txt; fi
  env OOTW_VARS=$W/cap/vars_16002_start.bin BGSEEN=/tmp/rc_seen.bin BGCAP=$W/cap/pc_uw_$t.bin ./oracle ../data 16002 200 - /dev/null /tmp/iu.txt >/dev/null 2>&1
done
cp /tmp/rc_seen.bin /tmp/rc_base.bin
rm -rf /tmp/rck; mkdir -p /tmp/rck
cat $W/cap/var/runs.txt $W/cap/var2/runs.txt | while read fn n; do k=$(basename $(dirname $fn))_$(basename $fn .txt)
  env OOTW_VARS=$W/cap/vars_16002_start.bin BGCAP=/dev/null BGKEYS=/tmp/rck/$k.bin timeout 60 ./oracle ../data 16002 $n - /dev/null $W/$fn >/dev/null 2>&1; done
python3 - <<'PY'
import glob, struct, collections
runs=collections.Counter()
for fn in glob.glob('/tmp/rck/*.bin'):
    b=open(fn,'rb').read(); ks=struct.unpack('<%dI'%(len(b)//4),b)
    for k in set(ks): runs[k]+=1
bb=open('/tmp/rc_base.bin','rb').read(); base=set(struct.unpack('<%dI'%(len(bb)//4),bb))
new=collections.Counter({k:c for k,c in runs.items() if k not in base})
top=[k for k,c in new.most_common(3500)]
open('/tmp/rc_want.bin','wb').write(b''.join(struct.pack('<I',k) for k in top))
print('base states',len(base),'recurring candidates >=4 runs',sum(1 for c in new.values() if c>=4),'chosen',len(top))
PY
cp /tmp/rc_base.bin /tmp/rc_seen2.bin; rm -f $W/cap/pc_var_sel.bin
cat $W/cap/var/runs.txt $W/cap/var2/runs.txt | while read fn n; do
  env OOTW_VARS=$W/cap/vars_16002_start.bin BGSEEN=/tmp/rc_seen2.bin BGWANT=/tmp/rc_want.bin BGCAP=/tmp/rc_pw.bin timeout 60 ./oracle ../data 16002 $n - /dev/null $W/$fn >/dev/null 2>&1
  python3 -c "
import struct,os
w=set(struct.unpack('<%dI'%(os.path.getsize('/tmp/rc_want.bin')//4),open('/tmp/rc_want.bin','rb').read()))
if os.path.exists('/tmp/rc_pw.bin'):
    b=open('/tmp/rc_pw.bin','rb').read(); o=open('$W/cap/pc_var_sel.bin','ab')
    for i in range(len(b)//64004):
        if struct.unpack('<I',b[i*64004:i*64004+4])[0] in w: o.write(b[i*64004:(i+1)*64004])
    os.remove('/tmp/rc_pw.bin')"
done
cd $W/game && python3 mkbg.py 150 | tail -1
