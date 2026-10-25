#include "check.h"
#include "form_check.h"

int main(void)
{
    CHECK(form_entity_id_valid(""));
    CHECK(form_entity_id_valid("sensor.some_name_1"));
    CHECK(!form_entity_id_valid("sensor"));
    CHECK(!form_entity_id_valid("Sensor.a"));
    CHECK(!form_entity_id_valid("sensor.a.b"));
    CHECK(!form_entity_id_valid(".a"));
    CHECK(!form_entity_id_valid("sensor."));
    CHECK(!form_entity_id_valid("sensor.a b"));
    CHECK(!form_entity_id_valid("sensor.a'"));
    CHECK(form_token_kept("", "http://h:8123", "http://h:8123"));
    CHECK(!form_token_kept("", "http://other:8123", "http://h:8123"));
    CHECK(!form_token_kept("abc", "http://h:8123", "http://h:8123"));
    CHECK(form_password_valid("") && form_password_valid("12345678") && !form_password_valid("short"));
    char url[32] = "192.168.1.5:8123";
    CHECK(form_url_valid(url, sizeof(url)));
    CHECK_STR(url, "http://192.168.1.5:8123");
    CHECK_DONE();
}
