#!/bin/bash
# bench.sh <label> [XCF...] : build current tree with flags, run free-play benchmark, print console frames for gf 100..250
cd /home/claude/w/sms
L=$1; shift
XCF="$*" bash build.sh >/tmp/build_$L.log 2>&1 || { tail /tmp/build_$L.log; exit 1; }
cp ootw.sms /tmp/rom_$L.sms
../tools/smstest/smstest /tmp/rom_$L.sms tests/an/free.txt > /tmp/run_$L.log
a=$(grep frames: /tmp/run_$L.log | head -1 | awk '{print $2}'); b=$(grep frames: /tmp/run_$L.log | tail -1 | awk '{print $2}')
echo "$L: $((b-a)) console frames for game frames 100-250 ($(grep 'fixed code' /tmp/build_$L.log | grep -o '[0-9]* bytes free'))"
