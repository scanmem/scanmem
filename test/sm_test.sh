#!/bin/bash
set -ev

# Start memfake. Without rseq: the kernel writes the current CPU number into
# glibc's rseq area, so memory would change between two scans of the same data.
GLIBC_TUNABLES=glibc.pthread.rseq=0 ./memfake 4 1 8 > /dev/null &
memfake_pid=$!
# a failing check exits the script (set -e): do not leave memfake behind
trap 'kill $memfake_pid 2> /dev/null' EXIT

# Wait until it has filled its memory and sleeps in pause()
until [ "$(cut -d' ' -f3 /proc/$memfake_pid/stat)" == S ]; do
    sleep 0.1
done

# Test runs

test_sm () {
    ../scanmem -p $memfake_pid -e -c "$1"
}

test_sm "option scan_data_type int8;0;exit"
test_sm "option scan_data_type int8;snapshot;exit"

test_sm "option scan_data_type int8;snapshot;1;exit"
test_sm "option scan_data_type int8;1;delete 0;1;exit"

test_sm "option scan_data_type int;1;exit"
test_sm "option scan_data_type float;1;exit"
test_sm "option scan_data_type number;1;exit"

huge_bytearray=""
huge_string=""
# 257 not a typo, forces full scan routine use
for ((i=0; i < 257; i++)); do
    huge_bytearray+="00 ?? "
    huge_string+="a"
done

test_sm "option scan_data_type bytearray;${huge_bytearray};exit"
test_sm "option scan_data_type string;\" ${huge_string};exit"

# With more than one thread, scans are split into chunks and skip, read and
# allocate differently; they must find exactly what the serial scans
# (`option threads 1`) find, including matches that cross the split points.
# The two runs must not overlap: only one of them can attach at a time.
test_threads () {
    local serial threaded
    serial=$(../scanmem -p $memfake_pid -e -c "option threads 1;$1;exit" 2>&1 | grep -v "option threads")
    threaded=$(../scanmem -p $memfake_pid -e -c "option threads 4;$1;exit" 2>&1 | grep -v "option threads")
    diff <(echo "$serial") <(echo "$threaded")
}

test_threads "option scan_data_type int8;1;list"
test_threads "option scan_data_type int;1;list"
test_threads "option scan_data_type float;1;list"
test_threads "option scan_data_type number;1;list"
test_threads "option scan_data_type number;-1"
test_threads "option scan_data_type int;0"
test_threads "option endianness 2;option scan_data_type int32;16777216;list"
test_threads "option scan_data_type bytearray;?? 02 03 ?? 05"
test_threads "option scan_data_type int;snapshot;=;1"
test_threads "option scan_data_type int8;snapshot;!= 0;> 9"
test_threads "option scan_data_type int;0;update;= 0"
# comparisons that run a scan routine over blocks of positions
test_threads "option scan_data_type int;> 1000000;< 2000000000"
test_threads "option scan_data_type number;-1000..1000000;!= 0"
test_threads "option endianness 2;option scan_data_type number;> 5;1..2000000000"
test_threads "option scan_data_type int16;snapshot;< 30000;> -5;+"
test_threads "option scan_data_type bytearray;${huge_bytearray};${huge_bytearray}"
# split points inside a match, followed by bytes that do not match: when the
# recorded bytes come out wrong, the second rescan drops matches
test_threads "option scan_data_type int64;0x0807060504030201;=;="
test_threads "option scan_data_type bytearray;${huge_bytearray}"
test_threads "option scan_data_type string;\" ${huge_string}"
