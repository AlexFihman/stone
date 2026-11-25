#!/bin/bash
# Continue a strip run after a power cut / crash. Safe to run at any time:
# refuses if the run is already active. Usage: resume.sh [DIR N H MINFREE_GB]
cd "$(dirname "$(readlink -f "$0")")" || exit 1
DIR=${1:-t1m}; N=${2:-1000000}; H=${3:-12000}; MIN=${4:-55}
# Set STONE_MOUNT to require that a data disk is mounted.
[ -z "${STONE_MOUNT:-}" ] || mountpoint -q "$STONE_MOUNT" || { echo "$STONE_MOUNT is not mounted"; exit 1; }
[ -f "$DIR/params" ] || { echo "no run in $DIR"; exit 1; }
for p in $(pgrep -x stone_strips); do
  [ "$(readlink -f /proc/$p/cwd)/$(tr '\0' '\n' </proc/$p/cmdline | sed -n 3p)" = "$PWD/$DIR" ] &&
    { echo "already running (pid $p)"; exit 1; }
done
exec 9>"$DIR/resume.lock"; flock -n 9 || { echo "another resume.sh is active"; exit 1; }
python3 - "$DIR" "$N" "$H" <<'PY' || { echo "CHECK FAILED - not starting, nothing was changed"; exit 1; }
import os,re,struct,sys
d,N,H=sys.argv[1],int(sys.argv[2]),int(sys.argv[3])
def frames(p):  # walk frame headers; the chain must end exactly at end of file
    sz=os.path.getsize(p); pos=0; off=[]
    with open(p,'rb') as f:
        while pos<sz:
            f.seek(pos); h=f.read(16)
            if len(h)<16: return None
            off.append(pos); pos+=16+struct.unpack('<QQ',h)[1]
    return off if pos==sz else None
st=sorted(int(m[1]) for m in (re.fullmatch(r'state_(\d+)\.zst',x) for x in os.listdir(d)) if m)
if not st: print('no finished strip yet: restarting from strip 0'); sys.exit(0)
y=st[-1]; print('last finished strip ends at row',y)
B=min(64,H)
while H%B: B-=1
bad=[]
if y<N and frames(f'{d}/state_{y}.zst') is None: bad.append(f'state_{y}.zst')
for y0 in range(0,y,H):
    idx=f'{d}/out_{y0}.zst.idx'; out=f'{d}/out_{y0}.zst'
    try:
        want=(N-y0+B-1)//B; raw=open(idx,'rb').read()
        ok=len(raw)==8*want
        if ok:   # last frame must end at end of file; full walk for the newest strip
            last=struct.unpack('<Q',raw[-8:])[0]
            with open(out,'rb') as f: f.seek(last); c=struct.unpack('<QQ',f.read(16))[1]
            ok=last+16+c==os.path.getsize(out)
            if ok and y0+H>=y: ok=frames(out)==list(struct.unpack(f'<{want}Q',raw))
        if not ok: bad.append(out)
    except (OSError,struct.error): bad.append(out)
if bad:
    print('damaged:',*bad)
    k=sorted(int(m[1]) for m in (re.fullmatch(r'state_(\d+)\.zst',x) for x in (os.listdir(d+'/keep') if os.path.isdir(d+'/keep') else [])) if m)
    print('saved states in keep/:',k)
    print('to fall back to row P (<%d): mv %s/state_%d.zst %s/bad_%d; ln %s/keep/state_P.zst %s/; run resume.sh again'%(y,d,y,d,y,d,d))
    sys.exit(1)
print('all',(y+H-1)//H,'finished strips look complete')
for s in st[:-1]: print('note: older state file left over, can be deleted: state_%d.zst'%s)
PY
while a=$(awk '/MemAvailable/{print int($2/1048576*1.073741824)}' /proc/meminfo); [ "$a" -lt "$MIN" ]; do
  echo "$(date '+%F %T') only $a GB RAM available, need $MIN: stop VMs (waiting)"; sleep 60; done
export OMP_NUM_THREADS=${OMP_NUM_THREADS:-8}
./keeper.sh "$DIR" $$ 9>&- &
echo "$(date '+%F %T') resuming"; exec ./stone_strips run "$DIR" "$N" "$H"
