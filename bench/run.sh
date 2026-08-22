#!/bin/bash
# Benchmark the two scan phases. Run from the repo root or from bench/.
#
# Initial scan  = sm_searchregions, walks every region once, runs the scan
#                 routine at every byte offset. Cost tracks region size.
# Narrowing     = sm_checkmatches, walks the existing match set. Cost tracks
#                 how many matches there are.
#
# Two scenarios, because they stress opposite ends:
#   dense   every slot planted, so the match array is enormous and the
#           narrowing scan dominates. Pathological, but it is the case that
#           catches buffer edge bugs.
#   sparse  a handful of planted values in the same region, which is what a
#           real scan looks like. Here the initial scan is nearly all of it.
#
# Each measurement gets a freshly started target. memfake rewrites its buffer
# when it mutates, so reusing one across runs means the second run scans for a
# value that is no longer there and reports a meaninglessly fast time.

set -u
cd "$(dirname "$0")"

SCANMEM=${SCANMEM:-../scanmem}
MEMFAKE=${MEMFAKE:-../test/memfake}
MB=${MB:-64}
REPS=${REPS:-3}
THREADS=${THREADS:-}
WIDTH=8
V1=$((0x5EED1234DEADBEEF))
V2=$((V1 + 1))
DENSE_SLOTS=$((MB * 1024 * 1024 / WIDTH))
SPARSE_SLOTS=${SPARSE_SLOTS:-1000}

SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO="sudo -n"

[ -x "$MEMFAKE" ] || { echo "build test/memfake first (make -C test memfake)"; exit 1; }
[ -x "$SCANMEM" ] || { echo "build scanmem first"; exit 1; }

pid=""; bg=""; ready=""; done_f=""; slots=0
start_target() {
    ready=$(mktemp); done_f=$(mktemp); rm -f "$done_f"
    "$MEMFAKE" --plant $V1 --count "$slots" --width $WIDTH --mb "$MB" \
        --ready-file "$ready" --done-file "$done_f" >/dev/null 2>&1 &
    bg=$!
    for _ in $(seq 1 400); do [ -s "$ready" ] && break; sleep 0.05; done
    pid=$(cat "$ready" 2>/dev/null)
    [ -n "$pid" ] || { echo "memfake did not start"; exit 1; }
}
stop_target() {
    [ -n "$pid" ] && kill "$pid" 2>/dev/null
    wait "$bg" 2>/dev/null
    rm -f "$ready" "$done_f"
    pid=""
}
trap stop_target EXIT

# `option threads N` only exists on builds that have it, so only send it when asked
opt_threads() {
    [ -n "$THREADS" ] && printf 'option threads %s\n' "$THREADS"
}

script_for() {
    case "$1" in
        scan)   printf '%soption scan_data_type int64\n%s\nexit\n' "$(opt_threads)" "$V1" ;;
        narrow) printf '%soption scan_data_type int64\n%s\nshell rm -f %s; kill -USR1 %s; while [ ! -s %s ]; do sleep 0.01; done\n%s\nexit\n' \
                    "$(opt_threads)" "$V1" "$done_f" "$pid" "$done_f" "$V2" ;;
    esac
}

# median of REPS runs, each against a fresh target
timed() {
    local kind=$1 times=() t start end
    for _ in $(seq 1 "$REPS"); do
        start_target
        start=$(date +%s.%N)
        script_for "$kind" | $SUDO "$SCANMEM" -p "$pid" >/dev/null 2>&1
        end=$(date +%s.%N)
        stop_target
        times+=("$(echo "$end - $start" | bc)")
    done
    printf '%s\n' "${times[@]}" | sort -g | awk -v n="$REPS" 'NR==int((n+1)/2)'
}

scenario() {
    local label=$1
    slots=$2
    echo "$label: ${MB}MB region, $slots planted slots, width $WIDTH, median of $REPS"
    local t_scan t_both t_narrow
    t_scan=$(timed scan)
    t_both=$(timed narrow)
    t_narrow=$(echo "$t_both - $t_scan" | bc)
    printf '  initial scan      %8.2fs\n' "$t_scan"
    printf '  scan + narrow     %8.2fs\n' "$t_both"
    printf '  narrow (derived)  %8.2fs\n' "$t_narrow"
    echo
}

[ -n "$THREADS" ] && echo "threads: $THREADS" && echo

scenario "dense " "$DENSE_SLOTS"
scenario "sparse" "$SPARSE_SLOTS"

if command -v strace >/dev/null 2>&1; then
    echo "syscalls, dense scan + narrow (fresh target):"
    slots=$DENSE_SLOTS
    start_target
    script_for narrow | $SUDO strace -f -c -e trace=pread64,process_vm_readv,ptrace \
        "$SCANMEM" -p "$pid" 2>&1 >/dev/null \
        | grep -E "pread64|process_vm_readv|ptrace|calls|^---" | head -8
    stop_target
else
    echo "(strace not installed, skipping syscall profile)"
fi
