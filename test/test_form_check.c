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
    char token[16] = "  abc \n";
    form_trim(token);
    CHECK_STR(token, "abc");
    char empty[4] = "";
    form_trim(empty);
    CHECK_STR(empty, "");
    char spaces[8] = "   ";
    form_trim(spaces);
    CHECK_STR(spaces, "");
    char blank[8] = "\t \r\n";
    form_trim(blank);
    CHECK(form_token_kept(blank, "http://h:8123", "http://h:8123"));
    char inner[16] = "a b\tc";
    form_trim(inner);
    CHECK_STR(inner, "a b\tc");
    CHECK(form_password_valid("") && form_password_valid("12345678") && !form_password_valid("short"));
    char url[32] = "192.168.1.5:8123";
    CHECK(form_url_valid(url, sizeof(url)));
    CHECK_STR(url, "http://192.168.1.5:8123");
    CHECK_DONE();
}
