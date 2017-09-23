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

# Clean up
kill $memfake_pid
