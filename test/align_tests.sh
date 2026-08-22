#!/bin/bash
# `option alignment N` makes the scan only look at addresses that are a
# multiple of N. The point is speed, but the thing worth asserting is that it
# finds exactly the values on the boundary and none of the ones off it, and
# that a match still narrows afterwards. The trailing bytes of a wide match
# still have to be recorded even though they are never tested, otherwise the
# old value cannot be rebuilt and the next scan drops the match.

set -u
cd "$(dirname "$0")"
. ./lib.sh

preflight

VAL=305419896            # 0x12345678, four distinct bytes
COUNT=8

# planted on a 4 byte boundary
start_memfake --plant "$VAL" --count "$COUNT" --width 4 --mb 4

for a in 1 2 4; do
    n=$(nth "$(run_scan "option scan_data_type int32\noption alignment $a\n$VAL\nexit")" 1)
    assert_ge "$n" "$COUNT" "aligned plant is found with alignment $a"
done

# narrowing has to survive, which it only does if the bytes behind each match
# were recorded while the scan skipped over them
out=$(run_scan "option scan_data_type int32\noption alignment 4\n$VAL\n$(mutate_cmd)\n$((VAL + 1))\nexit")
assert_eq "$(nth "$out" 2)" "$COUNT" "narrow after an aligned scan is exactly $COUNT"

stop_memfake

# same value, now one byte off the boundary every time
start_memfake --plant "$VAL" --count "$COUNT" --width 4 --mb 4 --skew 1

n1=$(nth "$(run_scan "option scan_data_type int32\noption alignment 1\n$VAL\nexit")" 1)
assert_ge "$n1" "$COUNT" "skewed plant is found when every address is looked at"

n4=$(nth "$(run_scan "option scan_data_type int32\noption alignment 4\n$VAL\nexit")" 1)
if [ "${n4:-0}" -lt "$COUNT" ]; then
    echo "ok: alignment 4 skips the skewed plant ($n4 left, was $n1)"
    checks=$((checks + 1))
else
    echo "FAIL: alignment 4 still found $n4, it should miss the skewed plant"
    checks=$((checks + 1)); fails=$((fails + 1))
fi

stop_memfake

# bad values are refused rather than quietly accepted
start_memfake --plant "$VAL" --count 1 --width 4 --mb 1
for bad in 0 3 16 -4 x; do
    out=$(printf 'option alignment %s\nexit\n' "$bad" \
          | $sudo_prefix timeout 60 $SCANMEM -p "$mf_pid" 2>&1)
    case "$out" in
        *"alignment must be"*) echo "ok: alignment $bad refused"; checks=$((checks + 1)) ;;
        *) echo "FAIL: alignment $bad was not refused"; checks=$((checks + 1)); fails=$((fails + 1)) ;;
    esac
done
stop_memfake

summary
