#!/bin/bash
# Scan correctness. Unlike the old smoke test these assert on the actual
# match counts, so a scan routine that silently returns the wrong set fails
# here instead of sailing through.
#
# The exact numbers come from the mutate-and-narrow trick: memfake plants a
# value N times, we scan for it, SIGUSR1 makes memfake rewrite only the
# planted slots, then we narrow. Incidental copies of the value elsewhere in
# the process (memfake's own locals, spilled registers) keep the old value
# and fall out, so the narrowed count is exactly N.

set -u
cd "$(dirname "$0")"
. ./lib.sh

preflight

# int64, the cleanest case: an 8 byte sentinel basically cannot collide
run_int_case() {
    local type=$1 width=$2 val=$3 count=$4
    start_memfake --plant "$val" --count "$count" --width "$width" --mb 4
    local n1=$((val + 1)) n2=$((val + 2))
    local out first
    if [ "$width" -eq 1 ]; then
        # a 1-byte value collides with incidental bytes all over the process. an
        # incidental that happens to drift val -> val+1 between the two scans
        # survives a single narrow as a phantom (this is a rare CI flake). narrow
        # twice: the same address would also have to drift val+1 -> val+2 in the
        # next window, which does not happen, so the second narrow lands on
        # exactly the planted set.
        out=$(run_scan "option scan_data_type $type\n$val\n$(mutate_cmd)\n$n1\n$(mutate_cmd)\n$n2\nexit")
        first=$(nth "$out" 1)
        assert_ge "$first" "$count" "$type: initial scan finds at least the $count planted"
        assert_eq "$(nth "$out" 3)" "$count" "$type: narrow after mutation is exactly $count"
    else
        out=$(run_scan "option scan_data_type $type\n$val\n$(mutate_cmd)\n$n1\nexit")
        first=$(nth "$out" 1)
        assert_ge "$first" "$count" "$type: initial scan finds at least the $count planted"
        assert_eq "$(nth "$out" 2)" "$count" "$type: narrow after mutation is exactly $count"
    fi
    stop_memfake
}

run_int_case int64 8 $((0x5EED1234DEADBEEF)) 100
run_int_case int32 4 $((0x5EED1234))        100
run_int_case int16 2 $((0x5EED))            100
run_int_case int8  1 $((0x42))              100

# floats go through a different comparison path (epsilon based), worth its own case
run_float_case() {
    local type=$1 width=$2 val=$3 next=$4 count=$5
    start_memfake --plant-float "$val" --count "$count" --width "$width" --mb 4
    local out
    out=$(run_scan "option scan_data_type $type\n$val\n$(mutate_cmd)\n$next\nexit")
    assert_ge "$(nth "$out" 1)" "$count" "$type: initial scan finds at least the $count planted"
    assert_eq "$(nth "$out" 2)" "$count" "$type: narrow after mutation is exactly $count"
    stop_memfake
}

run_float_case float32 4 1337.5 1338.5 100
run_float_case float64 8 1337.5 1338.5 100

# bytearray uses the wildcard capable routine, and >8 bytes forces the
# general loop rather than the power-of-two specialisations
bytearray_case() {
    local hex=$1 pattern=$2 count=$3 label=$4
    start_memfake --plant-bytes "$hex" --count "$count" --mb 4
    local out
    out=$(run_scan "option scan_data_type bytearray\n$pattern\nexit")
    assert_ge "$(nth "$out" 1)" "$count" "bytearray $label: finds at least the $count planted"
    stop_memfake
}

bytearray_case "deadbeef"                 "de ad be ef"                      100 "4 byte"
bytearray_case "deadbeefcafebabe"         "de ad be ef ca fe ba be"          100 "8 byte"
bytearray_case "deadbeefcafebabe11"       "de ad be ef ?? ?? ba be 11"       100 "wildcards"
bytearray_case "0011223344556677889900aa" "00 11 22 33 44 55 66 77 88 99 00 aa" 100 "12 byte, general loop"

# a value that was never planted must come back with nothing
start_memfake --plant $((0x5EED1234DEADBEEF)) --count 100 --width 8 --mb 4
out=$(run_scan "option scan_data_type int64\n1234567890123456789\nexit")
assert_eq "$(nth "$out" 1)" "0" "absent value returns no matches"
stop_memfake

# Dense case: every slot in the buffer holds the value, so the last element of
# every read window is a real match. Off-by-one errors at buffer edges hide
# completely in the sparse cases above (they were planted ~40KB apart) but show
# up loudly here. This is the test that catches a bad chunk boundary.
dense_slots=$((2 * 1024 * 1024 / 8))
start_memfake --plant $((0x5EED1234DEADBEEF)) --count "$dense_slots" --width 8 --mb 2
out=$(run_scan "option scan_data_type int64\n$((0x5EED1234DEADBEEF))\n$(mutate_cmd)\n$((0x5EED1234DEADBEEF + 1))\nexit")
assert_ge "$(nth "$out" 1)" "$dense_slots" "dense: initial scan finds every planted slot"
assert_eq "$(nth "$out" 2)" "$dense_slots" "dense: narrow keeps every slot, no edges dropped"
stop_memfake

# Thread count must not change the answer. The first scan is split into chunks
# and a match can straddle a chunk boundary, so a thread has to work out what
# is still carrying over from the chunk before it. Compare the whole list, a
# broken carry keeps the same number of matches but truncates the value.
thread_case() {
    local dtype=$1 pattern=$2 label=$3
    local whole split
    # One chunk big enough to swallow the region has no boundaries in it, so it
    # is the ground truth. Comparing two thread counts would not do: both go
    # through the same chunking, so a broken carry would corrupt both the same
    # way and they would still agree with each other.
    SCAN_ENV="SCANMEM_SCAN_CHUNK_BYTES=1073741824" \
        whole=$(run_list "option threads 1\noption scan_data_type $dtype\n$pattern\nlist\nexit")
    SCAN_ENV="SCANMEM_SCAN_CHUNK_BYTES=4096" \
        split=$(run_list "option threads 8\noption scan_data_type $dtype\n$pattern\nlist\nexit")
    assert_ge "$(printf '%s\n' "$whole" | grep -c .)" "1" "threads $label: unsplit run found something to compare"
    assert_eq "$(printf '%s' "$split" | md5sum)" "$(printf '%s' "$whole" | md5sum)" \
        "threads $label: split into 4096 byte chunks gives the same list"
}

start_memfake --plant-bytes "0011223344556677889900aa" --count 500 --mb 8
thread_case bytearray "00 11 22 33 44 55 66 77 88 99 00 aa" "bytearray 12 byte"
stop_memfake

start_memfake --plant $((0x5EED1234DEADBEEF)) --count 500 --width 8 --mb 8
thread_case int64 "$((0x5EED1234DEADBEEF))" "int64"
stop_memfake

summary
