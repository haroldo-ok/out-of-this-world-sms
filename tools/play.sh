#!/bin/bash
# play.sh inputs.txt nframes out.png -> screen timeline + contact sheet of last frames
cd /home/claude/w/rawgl
./oracle ../data 16002 $2 /tmp/p.bin /tmp/p.txt $1 >/dev/null 2>&1
awk 'BEGIN{l="x"} $2=="scr"{if($3!=l){printf "%s:%s ", n, $3; l=$3}} $2=="display"{n++} $2=="part"{printf "PART%s@%d ",$3,n}' /tmp/p.txt; echo
N=$2; python3 ../fview.py /tmp/p.bin $3 $((N-160)) $((N-120)) $((N-80)) $((N-40)) $((N-20)) $((N-1))
