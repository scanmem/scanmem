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

stop_memfake
summary
