#!/bin/bash

# test executable
TEST_EXEC="./tests/comp_tests/dense_access_static"

PAGERS=(
    "./apager"
    "./dpager"
    "./hpager_p0"
    "./hpager_p1"
    "./hpager_p2"
)

# output format for /usr/bin/time:
# RealTime UserTime SysTime MaxRSS MinorFaults MajorFaults
FORMAT_STRING="%e %U %S %M %R %F"

echo "Timing Test: $TEST_EXEC"
echo "-------------------------------------------------------------------------"
echo "Pager         Real(s) User(s) Sys(s) MaxRSS(KB) MinorFaults MajorFaults"
echo "-------------------------------------------------------------------------"

TMP_TIME_FILE=$(mktemp)

for pager in "${PAGERS[@]}"; do
    if [ ! -x "$pager" ]; then
        echo "Error: $pager not found or not executable. Skipping."
        continue
    fi


    # timing_output=$(/usr/bin/time -f "$pager ${FORMAT_STRING}" "$pager" "$TEST_EXEC" 2>&1)

    /usr/bin/time -f "${FORMAT_STRING}" -o "$TMP_TIME_FILE" "$pager" "$TEST_EXEC" > /dev/null 2>&1
    timing_output=$(cat "$TMP_TIME_FILE")

    printf "%-12s %s\n" "$pager" "$timing_output"

done

echo "-------------------------------------------------------------------------"
echo "Timing complete."