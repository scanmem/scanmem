#!/bin/bash
# Tests for the CLI commands, as opposed to scan correctness which lives in
# scan_tests.sh. Added while working the PR backlog: `reset keep-regions`
# (#145) and `read` (#346) both went in without any test at all.

set -u
cd "$(dirname "$0")"
. ./lib.sh

preflight

VAL=1234567
COUNT=64

# raw run, stderr included, so we can assert on error text
run_raw() {
    printf '%b\n' "$1" | $sudo_prefix timeout 120 $SCANMEM -p "$mf_pid" 2>&1
}

start_memfake --plant "$VAL" --count "$COUNT" --width 4 --mb 4

# ---- reset keep-regions (#145) ----

# control: without a reset the matches are still listed
out=$(run_list "option scan_data_type int32\n$VAL\nlist\nexit")
n=$(echo "$out" | grep -c . || true)
assert_ge "$n" 1 "control: matches are listed when nothing reset them"

# keep-regions still forgets the matches
out=$(run_list "option scan_data_type int32\n$VAL\nreset keep-regions\nlist\nexit")
n=$(echo "$out" | grep -c . || true)
assert_eq "$n" 0 "reset keep-regions forgets the matches"

# and the region list survives, so a second scan still finds the value.
# if regions had been dropped without a reread this would come back empty.
out=$(run_scan "option scan_data_type int32\n$VAL\nreset keep-regions\n$VAL\nexit")
first=$(nth "$out" 1)
second=$(nth "$out" 2)
assert_ge "$first" "$COUNT" "first scan finds the planted values"
assert_eq "$second" "$first" "scan after reset keep-regions finds the same count"

# plain reset rereads maps and still works
out=$(run_scan "option scan_data_type int32\n$VAL\nreset\n$VAL\nexit")
assert_eq "$(nth "$out" 2)" "$(nth "$out" 1)" "scan after a plain reset finds the same count"

# a typo must be refused, not silently treated as a plain reset
out=$(run_raw "reset keepregions\nexit")
echo "$out" | grep -q "unrecognised argument" \
    && assert_eq yes yes "reset rejects an unknown argument" \
    || assert_eq no yes "reset rejects an unknown argument"

# refusing it must also leave the matches alone
out=$(run_list "option scan_data_type int32\n$VAL\nreset keepregions\nlist\nexit")
n=$(echo "$out" | grep -c . || true)
assert_ge "$n" 1 "a refused reset does not drop the matches"

out=$(run_raw "reset a b c\nexit")
echo "$out" | grep -q "bad arguments" \
    && assert_eq yes yes "reset rejects too many arguments" \
    || assert_eq no yes "reset rejects too many arguments"

# ---- read (#346) ----

# grab a real address out of the match list to read from
first_addr=$(printf 'option scan_data_type int32\n%s\nlist\nexit\n' "$VAL" \
    | $sudo_prefix timeout 120 $SCANMEM -p "$mf_pid" 2>/dev/null \
    | sed -n 's/^\[ *[0-9]*\][ ,]*\([0-9a-f]*\),.*/\1/p' | head -1)
assert_ge "${#first_addr}" 4 "found an address to read from"

out=$(run_raw "read int32 $first_addr\nexit" | grep -E '^-?[0-9]+$' | tail -1)
assert_eq "$out" "$VAL" "read int32 returns the planted value"

# round trip against write, which is the whole point of having both
out=$(run_raw "write int32 $first_addr 999\nread int32 $first_addr\nexit" | grep -E '^-?[0-9]+$' | tail -1)
assert_eq "$out" "999" "read sees what write just wrote"

# a wider read at the same spot must not fault or truncate to the 32 bit value
out=$(run_raw "read int64 $first_addr\nexit" | grep -E '^-?[0-9]+$' | tail -1)
assert_ge "${#out}" 1 "read int64 at the same address produces a value"

# error paths. the original patch returned an uninitialised bool on success,
# so these also pin down that a bad command reports failure rather than luck.
for bad in "read" "read int32" "read bogus 1000" "read int32 zzz" "read int32 1000 extra"; do
    out=$(run_raw "$bad\nexit")
    echo "$out" | grep -q "error:" \
        && assert_eq yes yes "rejected: $bad" \
        || assert_eq no yes "rejected: $bad"
done

# write swaps bytes when reverse endianness is on, so read has to swap back
# or the pair stops round tripping. checks the claim in the code comment.
for e in 0 1 2; do
    out=$(run_raw "option endianness $e\nwrite int32 $first_addr 305419896\nread int32 $first_addr\nexit" \
          | grep -E '^-?[0-9]+$' | tail -1)
    assert_eq "$out" "305419896" "write then read round trips at endianness $e"
done

# reading with no process attached must say so, not just fail to read
out=$(printf 'read int32 1000\nexit\n' | $sudo_prefix timeout 120 $SCANMEM 2>&1)
echo "$out" | grep -q "no target set" \
    && assert_eq yes yes "read with no target explains itself" \
    || assert_eq no yes "read with no target explains itself"

stop_memfake
summary
