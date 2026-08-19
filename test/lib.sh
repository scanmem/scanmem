# shared bits for the scan tests
# sourced, not run

SCANMEM=${SCANMEM:-../scanmem}
MEMFAKE=${MEMFAKE:-./memfake}

fails=0
checks=0
mf_pid=""

# scanmem needs to ptrace a process it didn't fork, which yama blocks by
# default. CI runs the suite under sudo; if we can't get there just skip
# rather than reporting a red suite to someone who only ran `make check`.
sudo_prefix=""
preflight() {
    if [ "$(id -u)" -eq 0 ]; then
        sudo_prefix=""
    elif sudo -n true 2>/dev/null; then
        sudo_prefix="sudo -n"
    else
        scope=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo 0)
        if [ "$scope" != "0" ]; then
            echo "SKIP: need root or ptrace_scope=0 to attach (scope=$scope)"
            exit 77
        fi
    fi
}

start_memfake() {
    ready=$(mktemp)
    : > "$ready"
    done_file=$(mktemp)
    rm -f "$done_file"
    $MEMFAKE "$@" --ready-file "$ready" --done-file "$done_file" >/dev/null 2>&1 &
    mf_bg=$!
    # wait for it to say it's up instead of sleeping and hoping
    for _ in $(seq 1 100); do
        [ -s "$ready" ] && break
        sleep 0.05
    done
    mf_pid=$(cat "$ready" 2>/dev/null)
    rm -f "$ready"
    if [ -z "$mf_pid" ]; then
        echo "could not start memfake $*"
        exit 1
    fi
}

# shell snippet to paste into a scanmem script: mutate, then block until
# memfake confirms it finished, so the following scan sees a settled buffer
mutate_cmd() {
    echo "shell rm -f $done_file; kill -USR1 $mf_pid; while [ ! -s $done_file ]; do sleep 0.01; done"
}

stop_memfake() {
    rm -f "$done_file" 2>/dev/null
    [ -n "$mf_pid" ] && kill "$mf_pid" 2>/dev/null
    wait "$mf_bg" 2>/dev/null
    mf_pid=""
}

# run a scanmem script against the running memfake, echo the match counts
# it reported, one per line
run_scan() {
    printf '%b\n' "$1" | $sudo_prefix timeout 120 $SCANMEM -p "$mf_pid" 2>&1 \
        | sed -n 's/.*we currently have \([0-9]*\) matches.*/\1/p'
}

# run a scanmem script and echo each listed match, index stripped. Comparing
# these catches things a match count cannot: a dropped filler byte at a chunk
# boundary changes the recorded value while leaving the count alone.
run_list() {
    printf '%b\n' "$1" | $sudo_prefix ${SCAN_ENV:+env $SCAN_ENV} timeout 120 \
        $SCANMEM -p "$mf_pid" 2>/dev/null \
        | sed -n 's/^\[ *[0-9]*\]//p'
}

# nth line of counts (1-based)
nth() { echo "$1" | sed -n "${2}p"; }

assert_eq() {
    checks=$((checks + 1))
    if [ "$1" != "$2" ]; then
        echo "FAIL: $3 (expected $2, got $1)"
        fails=$((fails + 1))
    else
        echo "ok: $3"
    fi
}

assert_ge() {
    checks=$((checks + 1))
    if [ -z "$1" ] || [ "$1" -lt "$2" ] 2>/dev/null; then
        echo "FAIL: $3 (expected at least $2, got ${1:-none})"
        fails=$((fails + 1))
    else
        echo "ok: $3"
    fi
}

summary() {
    echo "---- $((checks - fails))/$checks passed"
    [ "$fails" -eq 0 ] || exit 1
}
