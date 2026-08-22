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

# ---- unsigned type names (#359) ----
# GameConqueror's type dropdown offers uint8..uint64. the parser only knew the
# signed names, so `write uint8 ...` was refused and the GUI's write silently
# did nothing, which is what #359 reports as "stops accepting new values".
for t in uint8 uint16 uint32 uint64 u8 u16 u32 u64; do
    out=$(run_raw "write $t $first_addr 1\nexit")
    echo "$out" | grep -q "bad data_type" \
        && assert_eq no yes "write accepts $t" \
        || assert_eq yes yes "write accepts $t"
done

# values that only fit unsigned must survive a round trip
out=$(run_raw "write uint8 $first_addr 200\nread uint8 $first_addr\nexit" | grep -E '^[0-9]+$' | tail -1)
assert_eq "$out" "200" "uint8 200 round trips"
# same byte read signed is the negative counterpart, so only the format changed
out=$(run_raw "read int8 $first_addr\nexit" | grep -E '^-?[0-9]+$' | tail -1)
assert_eq "$out" "-56" "the same byte read as int8 is -56"

out=$(run_raw "write uint32 $first_addr 4000000000\nread uint32 $first_addr\nexit" | grep -E '^[0-9]+$' | tail -1)
assert_eq "$out" "4000000000" "uint32 above INT32_MAX round trips"

out=$(run_raw "write uint64 $first_addr 18446744073709551615\nread uint64 $first_addr\nexit" | grep -E '^[0-9]+$' | tail -1)
assert_eq "$out" "18446744073709551615" "uint64 max round trips"

# ---- xor between scan rounds (#424) ----
# earlier tests wrote over one of the planted slots, so start clean
stop_memfake
start_memfake --plant "$VAL" --count "$COUNT" --width 4 --mb 4

NEXT=$((VAL + 1))
DELTA=$((VAL ^ NEXT))

# baseline: the ordinary narrow, so we know what the xor should reproduce
out=$(run_scan "option scan_data_type int32\n$VAL\n$(mutate_cmd)\n$NEXT\nexit")
expect=$(nth "$out" 2)
assert_ge "$expect" "$COUNT" "control: plain narrow finds the planted values"

stop_memfake
start_memfake --plant "$VAL" --count "$COUNT" --width 4 --mb 4
out=$(run_scan "option scan_data_type int32\n$VAL\n$(mutate_cmd)\n^ $DELTA\nexit")
assert_eq "$(nth "$out" 2)" "$expect" "one arg xor narrows to the same set"

# two arg form: the key cancels, so ^ n m must equal ^ (n^m)
stop_memfake
start_memfake --plant "$VAL" --count "$COUNT" --width 4 --mb 4
out=$(run_scan "option scan_data_type int32\n$VAL\n$(mutate_cmd)\n^ $VAL $NEXT\nexit")
assert_eq "$(nth "$out" 2)" "$expect" "two arg xor narrows to the same set"

# a wrong delta must not match the planted slots
stop_memfake
start_memfake --plant "$VAL" --count "$COUNT" --width 4 --mb 4
out=$(run_scan "option scan_data_type int32\n$VAL\n$(mutate_cmd)\n^ $((DELTA ^ 0xff))\nexit")
second=$(nth "$out" 2)
[ "${second:-0}" -lt "$expect" ] \
    && assert_eq yes yes "a wrong xor delta does not match" \
    || assert_eq no yes "a wrong xor delta does not match"

# xor cannot be a first scan, there is no old value to compare against
out=$(run_raw "^ 5\nexit")
echo "$out" | grep -q "without matches" \
    && assert_eq yes yes "xor is refused as a first scan" \
    || assert_eq no yes "xor is refused as a first scan"

out=$(run_raw "option scan_data_type int32\n$VAL\n^\nexit")
echo "$out" | grep -q "one or two values" \
    && assert_eq yes yes "xor with no value is refused" \
    || assert_eq no yes "xor with no value is refused"

out=$(run_raw "option scan_data_type int32\n$VAL\n^ 1 2 3\nexit")
echo "$out" | grep -q "too many values" \
    && assert_eq yes yes "xor with three values is refused" \
    || assert_eq no yes "xor with three values is refused"

# the other operators must still reject a second value
out=$(run_raw "option scan_data_type int32\n$VAL\n+ 1 2\nexit")
echo "$out" | grep -q "too many values" \
    && assert_eq yes yes "+ still rejects two values" \
    || assert_eq no yes "+ still rejects two values"

# ---- scan undo / redo (#389) ----
stop_memfake
start_memfake --plant "$VAL" --count "$COUNT" --width 4 --mb 4

# scan, narrow, then step back and forward again
out=$(run_scan "option undo_limit 5\noption scan_data_type int32\n$VAL\n$(mutate_cmd)\n$NEXT\nundo\nredo\nexit")
c1=$(nth "$out" 1); c2=$(nth "$out" 2)
assert_eq "$(nth "$out" 3)" "$c1" "undo restores the pre-narrow match count"
assert_eq "$(nth "$out" 4)" "$c2" "redo returns to the narrowed match count"
[ "${c2:-0}" -lt "${c1:-0}" ] \
    && assert_eq yes yes "control: the narrow actually reduced the set" \
    || assert_eq no yes "control: the narrow actually reduced the set"

# undoing past the first scan lands on an empty set, not an error
stop_memfake
start_memfake --plant "$VAL" --count "$COUNT" --width 4 --mb 4
out=$(run_scan "option undo_limit 5\noption scan_data_type int32\n$VAL\nundo\nexit")
assert_eq "$(nth "$out" 2)" "0" "undo past the first scan gives an empty set"

# a fresh scan must throw away the redo chain
stop_memfake
start_memfake --plant "$VAL" --count "$COUNT" --width 4 --mb 4
out=$(run_raw "option undo_limit 5\noption scan_data_type int32\n$VAL\n$(mutate_cmd)\n$NEXT\nundo\n$NEXT\nredo\nexit")
echo "$out" | grep -q "nothing to redo" \
    && assert_eq yes yes "a new scan discards the redo chain" \
    || assert_eq no yes "a new scan discards the redo chain"

# the limit is honoured: with 1 remembered scan only one step back works
stop_memfake
start_memfake --plant "$VAL" --count "$COUNT" --width 4 --mb 4
out=$(run_raw "option undo_limit 1\noption scan_data_type int32\n$VAL\n$(mutate_cmd)\n$NEXT\nundo\nundo\nexit")
echo "$out" | grep -q "nothing to undo" \
    && assert_eq yes yes "undo_limit caps how far back you can go" \
    || assert_eq no yes "undo_limit caps how far back you can go"

# off by default
out=$(run_raw "option scan_data_type int32\n$VAL\nundo\nexit")
echo "$out" | grep -q "undo is disabled" \
    && assert_eq yes yes "undo is off unless undo_limit is set" \
    || assert_eq no yes "undo is off unless undo_limit is set"

# reset must drop the history, the snapshots no longer mean anything
out=$(run_raw "option undo_limit 5\noption scan_data_type int32\n$VAL\nreset\nundo\nexit")
echo "$out" | grep -q "nothing to undo" \
    && assert_eq yes yes "reset clears the undo history" \
    || assert_eq no yes "reset clears the undo history"

out=$(run_raw "option undo_limit 5\noption scan_data_type int32\n$VAL\nreset keep-regions\nundo\nexit")
echo "$out" | grep -q "nothing to undo" \
    && assert_eq yes yes "reset keep-regions clears the undo history" \
    || assert_eq no yes "reset keep-regions clears the undo history"

# a negative limit must not wrap round to 65535
for bad in -1 abc 70000 "12x"; do
    out=$(run_raw "option undo_limit $bad\nexit")
    echo "$out" | grep -q "undo_limit must be between" \
        && assert_eq yes yes "option undo_limit rejects $bad" \
        || assert_eq no yes "option undo_limit rejects $bad"
done

# ---- memdiff (#403) ----
stop_memfake
start_memfake --plant "$VAL" --count "$COUNT" --width 4 --mb 4

md_addr=$(printf 'option scan_data_type int32\n%s\nlist\nexit\n' "$VAL" \
    | $sudo_prefix timeout 120 $SCANMEM -p "$mf_pid" 2>/dev/null \
    | sed -n 's/^\[ *[0-9]*\][ ,]*\([0-9a-f]*\),.*/\1/p' | head -1)
assert_ge "${#md_addr}" 4 "found an address to watch"

# memdiff runs until interrupted, so cap it
md_run() { printf '%b\n' "$1" | $sudo_prefix timeout "${2:-4}" $SCANMEM -p "$mf_pid" 2>&1; }

# the table must start at the requested address, not one byte in. VAL is
# 0x12d687 so the first four bytes little endian are 87 d6 12 00.
out=$(md_run "memdiff $md_addr 16" 3 | grep -iE "^0x$md_addr: " | head -1)
echo "$out" | grep -qiE ": 87 d6 12 00 " \
    && assert_eq yes yes "table starts at the first byte of the region" \
    || assert_eq no yes "table starts at the first byte of the region"

# a short trailing row must be padded so the ascii column still lines up
out=$(md_run "memdiff $md_addr 20" 3 | grep -icE "^0x[0-9a-f]+: " || true)
assert_ge "$out" 2 "a length that is not a multiple of 16 emits a short row"

# and it must actually notice a change
( sleep 2; kill -USR1 "$mf_pid" 2>/dev/null ) &
out=$(md_run "memdiff $md_addr 32 list" 5 | grep -c "=>" || true)
assert_ge "$out" 1 "list mode reports a byte that changed"

# no colour when the output is not a terminal, the gui parses this
out=$(md_run "memdiff $md_addr 16" 3 | grep -c "$(printf '\033')" || true)
assert_eq "$out" "0" "no escape codes when stdout is not a tty"

for bad in "memdiff" "memdiff $md_addr" "memdiff $md_addr 0" \
           "memdiff $md_addr 99999999999" "memdiff $md_addr 16 bogus" "memdiff zz 16"; do
    out=$(run_raw "$bad\nexit")
    echo "$out" | grep -q "error:" \
        && assert_eq yes yes "rejected: $bad" \
        || assert_eq no yes "rejected: $bad"
done

# ---- sm_reset is a public API symbol (#312) ----
# the whole point of the PR is that a front end can call this without going
# through the command parser, so check it actually made it out of the .so
lib=../.libs/libscanmem.so
if [ -f "$lib" ] && command -v nm >/dev/null 2>&1; then
    nm -D --defined-only "$lib" 2>/dev/null | grep -q " T sm_reset$" \
        && assert_eq yes yes "sm_reset is exported from libscanmem" \
        || assert_eq no yes "sm_reset is exported from libscanmem"
else
    echo "skip: no shared lib or nm, not checking sm_reset export"
fi

stop_memfake
summary
