#!/bin/bash
set -ev

# Start memfake
./memfake 4 1 &
memfake_pid=$!

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

# Unsigned integer writes (#359). uint8/16/32/64 used to be rejected as a bad
# data_type, so a value shown as unsigned in GameConqueror never got written.
# Write through the first writable anonymous mapping of the target; under -e a
# rejected type or a failed write aborts the test.
uaddr=$(awk '$2 ~ /rw-p/ && $6=="" {print $1; exit}' /proc/$memfake_pid/maps | cut -d- -f1)
test -n "$uaddr"
test_sm "write uint8 0x$uaddr 200"
test_sm "write uint16 0x$uaddr 60000"
test_sm "write uint32 0x$uaddr 4000000000"
test_sm "write uint64 0x$uaddr 18000000000000000000"
test_sm "write u8 0x$uaddr 255"

# Clean up
kill $memfake_pid
