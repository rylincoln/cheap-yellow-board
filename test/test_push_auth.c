#include "check.h"
#include "push_auth.h"

#define SHA "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
#define CHALLENGE "00112233445566778899aabbccddeeff"

int main(void)
{
    char out[512];
    const char *expected = "cyd-update-1\n" CHALLENGE "\n1187968\n" SHA "\nhome_dashboard\n1761488715\n";
    size_t n = push_auth_message(out, sizeof(out), CHALLENGE, 1187968, SHA, "home_dashboard", 1761488715);
    CHECK_STR(out, expected);
    CHECK(n == strlen(expected));
    CHECK(push_auth_message(out, 20, CHALLENGE, 1187968, SHA, "home_dashboard", 1761488715) == 0);
    CHECK(push_auth_message(out, sizeof(out), CHALLENGE, 1187968, SHA, "a\nb", 1761488715) == 0);

    uint8_t a[32], b[32];
    CHECK(push_auth_parse_hex32(SHA, a));
    CHECK(a[0] == 0xe3 && a[31] == 0x55);
    CHECK(push_auth_parse_hex32("E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855", b));
    CHECK(push_auth_equal(a, b));
    CHECK(!push_auth_parse_hex32("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b85", b));
    CHECK(!push_auth_parse_hex32(SHA "5", b));
    CHECK(!push_auth_parse_hex32("g3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", b));
    CHECK(!push_auth_parse_hex32("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b85g", b));
    b[31] ^= 1;
    CHECK(!push_auth_equal(a, b));

    CHECK(push_auth_key_valid("ABCD1234EFGH5678JKMN"));
    CHECK(push_auth_key_valid("0123456789ABCDEFGHJK"));
    CHECK(!push_auth_key_valid("ABCD1234EFGH5678JKM"));   /* 19 */
    CHECK(!push_auth_key_valid("ABCD1234EFGH5678JKMNP")); /* 21 */
    CHECK(!push_auth_key_valid("abcd1234efgh5678jkmn"));  /* not normalized */
    CHECK(!push_auth_key_valid("ABCD-1234-EFGH-5678-J"));
    CHECK(!push_auth_key_valid("ABCD1234EFGH5678JKMU"));  /* U isn't in the alphabet */
    CHECK(!push_auth_key_valid("ABCD1234EFGH5678JKMO"));
    CHECK(!push_auth_key_valid(""));

    uint8_t random[16];
    for (int i = 0; i < 16; ++i) {
        random[i] = (uint8_t)i;
    }
    push_challenge_t c = {0};
    push_challenge_issue(&c, random, 0);
    CHECK_STR(c.hex, "000102030405060708090a0b0c0d0e0f");
    CHECK(push_challenge_take(&c, "000102030405060708090a0b0c0d0e0f", 59999999));
    CHECK(!push_challenge_take(&c, "000102030405060708090a0b0c0d0e0f", 59999999));
    push_challenge_issue(&c, random, 0);
    CHECK(!push_challenge_take(&c, "000102030405060708090a0b0c0d0e0e", 1));
    CHECK(!push_challenge_take(&c, "000102030405060708090a0b0c0d0e0f", 1));
    push_challenge_issue(&c, random, 0);
    CHECK(!push_challenge_take(&c, "000102030405060708090a0b0c0d0e0f", 60000001));
    CHECK_DONE();
}
