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
    local next=$((val + 1))
    local out
    out=$(run_scan "option scan_data_type $type\n$val\n$(mutate_cmd)\n$next\nexit")
    local first last
    first=$(nth "$out" 1)
    last=$(nth "$out" 2)
    assert_ge "$first" "$count" "$type: initial scan finds at least the $count planted"
    assert_eq "$last" "$count" "$type: narrow after mutation is exactly $count"
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

summary
