#!/bin/sh
# Builds and runs every host-side test with the system C compiler. Each test
# is two lines, not one `&&` chain, so that set -e stops on a failed build.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host-tests

cc -std=c11 -Wall -Wextra -Werror -Ilauncher/main test/test_form_check.c launcher/main/form_check.c -o build/host-tests/form_check
build/host-tests/form_check

cc -std=c11 -Wall -Wextra -Werror -Iapps/home_dashboard/main test/test_readings.c apps/home_dashboard/main/readings.c -lm -o build/host-tests/readings
build/host-tests/readings

echo "host tests passed"
