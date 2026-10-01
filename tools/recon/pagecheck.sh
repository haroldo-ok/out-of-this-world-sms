#!/bin/bash
# pagecheck.sh <label> [flags]: build, hash every displayed picture over the free-play script -> /tmp/ph_<label>.txt
cd /home/claude/w/sms; L=$1; shift
XCF="$*" bash build.sh >/tmp/build_$L.log 2>&1 || { tail -3 /tmp/build_$L.log; exit 1; }
sed "s#/tmp/HOOK#/tmp/ph_$L.txt#" ${SCRIPT:-tests/an/pagehash.txt} > /tmp/ph_$L.script
../tools/smstest/smstest ootw.sms /tmp/ph_$L.script | grep -E "hookstop|FAIL"
