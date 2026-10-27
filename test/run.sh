#!/bin/sh
# Builds and runs every host-side test with the system C compiler. Each test
# is two lines, not one `&&` chain, so that set -e stops on a failed build.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host-tests

cc -std=c11 -Wall -Wextra -Werror -Ilauncher/main test/test_form_check.c launcher/main/form_check.c -o build/host-tests/form_check
build/host-tests/form_check

cc -std=c11 -Wall -Wextra -Werror -Icomponents/ha_glance/include test/test_readings.c components/ha_glance/readings.c -lm -o build/host-tests/readings
build/host-tests/readings

cc -std=c11 -Wall -Wextra -Werror -Icomponents/ha_glance/include -Iapps/home_dashboard/main test/test_bigfont.c components/ha_glance/bigfont.c apps/home_dashboard/main/bigfont_data.c -o build/host-tests/bigfont
build/host-tests/bigfont

cc -std=c11 -Wall -Wextra -Werror -Icomponents/ha_glance/include test/test_glance_tile.c components/ha_glance/glance_tile.c components/ha_glance/bigfont.c -o build/host-tests/glance_tile
build/host-tests/glance_tile

cc -std=c11 -Wall -Wextra -Werror -Icomponents/ha_glance/include -Iapps/home_dashboard/main test/test_glance_layout.c apps/home_dashboard/main/glance_layout.c components/ha_glance/glance_tile.c components/ha_glance/readings.c components/ha_glance/bigfont.c apps/home_dashboard/main/bigfont_data.c -lm -o build/host-tests/glance_layout
build/host-tests/glance_layout

cc -std=c11 -Wall -Wextra -Werror -Iapps/home_dashboard/main -Icomponents/json_scan/include test/test_wildlife.c apps/home_dashboard/main/wildlife.c components/json_scan/json_scan.c -o build/host-tests/wildlife
build/host-tests/wildlife

cc -std=c11 -Wall -Wextra -Werror -Icomponents/cyd_update test/test_push_auth.c components/cyd_update/push_auth.c -o build/host-tests/push_auth
build/host-tests/push_auth

cc -std=c11 -Wall -Wextra -Werror -Icomponents/cyd_update test/test_image_desc.c components/cyd_update/image_desc.c -o build/host-tests/image_desc
build/host-tests/image_desc

cc -std=c11 -Wall -Wextra -Werror -Icomponents/cyd_update -Icomponents/json_scan/include test/test_update_rules.c components/cyd_update/update_rules.c components/json_scan/json_scan.c -o build/host-tests/update_rules
build/host-tests/update_rules

python3 tools/test_cyd_push.py

echo "host tests passed"
