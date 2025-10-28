#!/bin/bash

TEST_EXEC="./tests/${1:-simple}"
FORMAT_STRING="%e %U %S %M %R %F"
TMP_TIME_FILE=$(mktemp)

echo ${TEST_EXEC}_static versus ${TEST_EXEC}_dyn
echo "Timing Comparison: Static vs. Dynamic (1000 Runs)"
echo "-------------------------------------------------------------------------"
echo "Test Run          Real(s) User(s) Sys(s) MaxRSS(KB) MinorFaults MajorFaults"
echo "-------------------------------------------------------------------------"

# Run the static test 1000 times
/usr/bin/time -f "${FORMAT_STRING}" -o "$TMP_TIME_FILE" \
    bash -c 'for i in $(seq 1 1000); do ./apager "$1" > /dev/null 2>&1; done' _ "${TEST_EXEC}_static"
timing_output=$(cat "$TMP_TIME_FILE")
printf "%-17s %s\n" "apager (static)" "$timing_output"

# Run the dynamic test 1000 times
/usr/bin/time -f "${FORMAT_STRING}" -o "$TMP_TIME_FILE" \
    bash -c 'for i in $(seq 1 1000); do ./apager "$1" > /dev/null 2>&1; done' _ "${TEST_EXEC}_dyn"
timing_output=$(cat "$TMP_TIME_FILE")
printf "%-17s %s\n" "apager (dynamic)" "$timing_output"

echo "-------------------------------------------------------------------------"
rm "$TMP_TIME_FILE"