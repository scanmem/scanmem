#!/bin/bash
# `range` crops the regions list to an address window. The thing worth
# checking is that cropping does not lose or invent matches: splitting a
# window in two has to account for exactly the matches the whole window
# finds, and the parser has to refuse the obvious bad input.

set -u

SM=../scanmem
fails=0

./memfake 4 0 &
pid=$!
trap 'kill $pid 2>/dev/null' EXIT
sleep 1

sm () {   # run a command list against the target, print everything
    $SM -p $pid -e -c "$1" 2>&1
}

matches () {   # count of matches the last scan reported
    sm "$1" | grep -oE 'we currently have [0-9]+ matches' | tail -1 \
        | grep -oE '[0-9]+'
}

check () {   # description, expected, actual
    if [ "$2" = "$3" ]; then
        echo "PASS: $1"
    else
        echo "FAIL: $1 (expected '$2', got '$3')"
        fails=$((fails + 1))
    fi
}

# scanning needs ptrace, which usually means root. CI runs this under sudo,
# a plain `make check` does not, so say so and skip instead of failing.
if sm "option scan_data_type int;0;exit" | grep -q 'failed to attach'; then
    echo "cannot attach to the target, run this as root (see ptrace_scope)"
    exit 77
fi

# the biggest region is memfake's 4MB array, work inside that
line=$(sm "lregions;exit" | grep -E '^\[ *[0-9]+\]' \
       | sort -t, -k2 -n -r | head -1)
start=$(echo "$line" | sed -E 's/^\[ *[0-9]+\] *//; s/,.*//')
if [ -z "$start" ]; then
    echo "FAIL: could not find a region to work with"
    exit 1
fi

# a 64k window inside it, and its two halves
lo=$start
mid=$(printf '%x' $((0x$start + 0x8000)))
hi=$(printf '%x'  $((0x$start + 0x10000)))

whole=$(matches "range $lo $hi;option scan_data_type int;0;exit")
first=$(matches "range $lo $mid;option scan_data_type int;0;exit")
second=$(matches "range $mid $hi;option scan_data_type int;0;exit")

echo "window $lo-$hi: whole=$whole first=$first second=$second"
if [ -z "$whole" ] || [ -z "$first" ] || [ -z "$second" ]; then
    echo "FAIL: no match counts came back"
    exit 1
fi

# a value spanning the split point belongs to neither half, so the halves
# can come up short by at most the 3 bytes an int can straddle by
diff=$((whole - first - second))
if [ "$diff" -ge 0 ] && [ "$diff" -le 3 ]; then
    echo "PASS: halves account for the whole window (off by $diff)"
else
    echo "FAIL: halves do not add up, whole=$whole first=$first second=$second"
    fails=$((fails + 1))
fi

# cropping has to actually crop
[ "$first" -lt "$whole" ] \
    && echo "PASS: half the window finds fewer matches" \
    || { echo "FAIL: half the window found $first, whole found $whole"; fails=$((fails + 1)); }

# a window with nothing mapped in it leaves no regions
out=$(sm "range 10 20;exit")
check "empty window warns" "yes" \
      "$(echo "$out" | grep -q 'nothing is mapped' && echo yes || echo no)"

# and then a scan there finds nothing
none=$(matches "range 10 20;option scan_data_type int;0;exit")
check "empty window finds nothing" "0" "${none:-0}"

# bad input
check "backwards range rejected" "yes" \
      "$(sm "range $hi $lo;exit" | grep -q 'has to be above' && echo yes || echo no)"
check "non hex rejected" "yes" \
      "$(sm "range zz $hi;exit" | grep -q 'bad start address' && echo yes || echo no)"
check "missing argument rejected" "yes" \
      "$(sm "range $lo;exit" | grep -q 'expected two addresses' && echo yes || echo no)"

# cropping with matches already on the list has to throw away the ones that
# fall outside, so a scan then crop then rescan lands on the same count as
# scanning the cropped window from the start
cropped_after=$(matches "option scan_data_type int;0;range $lo $mid;=;exit")
check "cropping an existing match list keeps only what is in range" \
      "$first" "$cropped_after"

# reset brings the full region list back
before=$(sm "lregions;exit" | grep -cE '^\[ *[0-9]+\]')
after=$(sm "range $lo $mid;reset;lregions;exit" | grep -cE '^\[ *[0-9]+\]')
check "reset restores the regions" "$before" "$after"

echo "---"
if [ "$fails" -eq 0 ]; then
    echo "all range tests passed"
    exit 0
fi
echo "$fails range test(s) failed"
exit 1
