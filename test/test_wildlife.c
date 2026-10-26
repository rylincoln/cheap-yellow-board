#include "check.h"
#include "wildlife.h"

/* A reply shaped like the gallery's GET /frame/wildlife, with three birds. */
static const char THREE_BIRDS[] =
    "{\"birds\": [\n"
    "  {\"audio_url\": \"/audio/13621\", \"confidence\": \"46%\", \"label\": \"Common Raven\", "
    "\"photo_url\": \"/species_photo/comrav\", \"time\": \"5:21 PM\"},\n"
    "  {\"audio_url\": \"/audio/13617\", \"confidence\": \"75%\", \"label\": \"Steller's Jay\", "
    "\"photo_url\": \"/species_photo/stejay\", \"time\": \"4:42 PM\"},\n"
    "  {\"audio_url\": \"/audio/13580\", \"confidence\": \"91%\", \"label\": \"Dark-eyed Junco\", "
    "\"photo_url\": null, \"time\": \"Oct 23, 10:52 PM\"}\n"
    " ],\n"
    " \"sighting\": {\"camera\": \"feeder\", \"confidence\": \"74%\", \"image_url\": \"/image/13437\", "
    "\"label\": \"Mule deer\", \"thumb_url\": \"/thumb/13437\", \"time\": \"Oct 23, 10:52 PM\", "
    "\"ts\": \"2026-10-23T22:52:40.000000\"},\n"
    " \"sightings\": [{\"camera\": \"feeder\", \"label\": \"Mule deer\", \"image_url\": \"/image/13437\"}, "
    "{\"camera\": \"feeder\", \"label\": \"Gray fox\", \"image_url\": \"/image/13401\"}]}";

/* Parses a reply held in a C string into birds; returns what the parser does. */
static int parse(const char *reply, wildlife_bird_t *birds, int max)
{
    return wildlife_parse_birds(reply, strlen(reply), birds, max);
}

/* Parses {"birds": [bird_json]} into one bird; returns how many it gave. */
static int parse_one(const char *bird_json, wildlife_bird_t *bird)
{
    char reply[512];
    snprintf(reply, sizeof(reply), "{\"birds\": [%s]}", bird_json);
    memset(bird, 0, sizeof(*bird));
    return parse(reply, bird, 1);
}

/* Checks the label a bird with only that JSON string as its label gets. */
static void label(const char *label_json, const char *want)
{
    char bird_json[256];
    wildlife_bird_t bird;
    snprintf(bird_json, sizeof(bird_json), "{\"label\": %s}", label_json);
    CHECK(parse_one(bird_json, &bird) == 1);
    CHECK_STR(bird.label, want);
}

/* Checks the photo_url a bird gets from that JSON value. */
static void photo(const char *photo_json, const char *want)
{
    char bird_json[256];
    wildlife_bird_t bird;
    snprintf(bird_json, sizeof(bird_json), "{\"label\": \"Wren\", \"photo_url\": %s}", photo_json);
    CHECK(parse_one(bird_json, &bird) == 1);
    CHECK_STR(bird.photo_url, want);
}

/* Checks the row drawn for a bird under "also heard". */
static void heard(const char *bird_label, const char *time, int columns, const char *want)
{
    wildlife_bird_t bird = {0};
    char row[32];
    snprintf(bird.label, sizeof(bird.label), "%s", bird_label);
    snprintf(bird.time, sizeof(bird.time), "%s", time);
    wildlife_heard_row(&bird, columns, row, sizeof(row));
    CHECK_STR(row, want);
}

int main(void)
{
    wildlife_bird_t birds[WILDLIFE_MAX_BIRDS];
    char text[512];
    char out[64];
    int n;

    /* The sample's shape: other members and audio_url are skipped. */
    n = parse(THREE_BIRDS, birds, WILDLIFE_MAX_BIRDS);
    CHECK(n == 3);
    CHECK_STR(birds[0].label, "Common Raven");
    CHECK_STR(birds[0].time, "5:21 PM");
    CHECK_STR(birds[0].confidence, "46%");
    CHECK_STR(birds[0].photo_url, "/species_photo/comrav");
    CHECK_STR(birds[1].label, "Steller's Jay");
    CHECK_STR(birds[1].time, "4:42 PM");
    CHECK_STR(birds[1].confidence, "75%");
    CHECK_STR(birds[1].photo_url, "/species_photo/stejay");
    CHECK_STR(birds[2].label, "Dark-eyed Junco");
    CHECK_STR(birds[2].time, "Oct 23, 10:52 PM");
    CHECK_STR(birds[2].confidence, "91%");
    CHECK_STR(birds[2].photo_url, "");
    /* Members before birds are skipped whole too. */
    n = parse("{\"sightings\": [{\"label\": \"Mule deer\", \"n\": [1, {\"x\": null}]}], "
              "\"sighting\": {\"label\": \"Gray fox\"}, \"birds\": [{\"label\": \"Bushtit\"}]}",
              birds, WILDLIFE_MAX_BIRDS);
    CHECK(n == 1);
    CHECK_STR(birds[0].label, "Bushtit");

    /* ASCII stand-ins, from \u escapes and from raw UTF-8. */
    label("\"Steller\\u2019s Jay\"", "Steller's Jay");
    label("\"Steller\xE2\x80\x99s Jay\"", "Steller's Jay");
    label("\"\xF0\x9F\x90\xA6 Wren\"", "? Wren");
    label("\"\\ud83d\\udc26 Wren\"", "? Wren");
    label("\"Caf\xC3\xA9 Wren\"", "Caf? Wren");
    label("\"Caf\\u00e9 Wren\"", "Caf? Wren");
    label("\"\\u2018Akiapola\\u02bbau\\u2019 x\\u02bcy \\u201cType\\u00a03\\u201d \\u2013 x \\u2014 y\"",
          "'Akiapola?au' x'y \"Type 3\" - x - y");
    label("\"\xE2\x80\x9C" "Red\xE2\x80\x9D" " \xE2\x80\x93 Crossbill\xC2\xA0" "3\"",
          "\"Red\" - Crossbill 3");
    /* Malformed UTF-8 is one '?' a sequence: a stray continuation byte, a
       sequence cut short, an overlong form and a lone surrogate. */
    label("\"A\x80" "B\"", "A?B");
    label("\"A\xE2\x80" "B\"", "A?B");
    label("\"A\xC0\xAF" "B\"", "A??B");
    label("\"A\xED\xA0\x80" "B\"", "A?B");
    label("\"A\\ud83d B\"", "A? B");
    label("\"A\\udc26B\"", "A?B");
    /* Escapes: quotes and slashes kept, control characters become '?'. */
    label("\"A\\/B \\\"C\\\" \\\\D\"", "A/B \"C\" \\D");
    label("\"Tab\\there\\n\"", "Tab?here?");
    label("\"Del\x7F\"", "Del?");

    /* A bird without a usable label is left out, and the others kept. */
    char label47[48];
    char label48[49];
    memset(label47, 'w', 47);
    label47[47] = '\0';
    memset(label48, 'w', 48);
    label48[48] = '\0';
    snprintf(text, sizeof(text),
             "{\"birds\": [{\"label\": \"Wren\"}, {\"time\": \"5:00 PM\"}, {\"label\": \"Robin\"}, "
             "{\"label\": \"\"}, {\"label\": \"Finch\"}, {\"label\": \"%s\"}, {\"label\": 7}, "
             "{\"label\": null}, 12, {\"label\": \"Jay\"}, {\"label\": \"%s\"}]}",
             label48, label47);
    n = parse(text, birds, WILDLIFE_MAX_BIRDS);
    CHECK(n == 5);
    CHECK_STR(birds[0].label, "Wren");
    CHECK_STR(birds[1].label, "Robin");
    CHECK_STR(birds[2].label, "Finch");
    CHECK_STR(birds[3].label, "Jay");
    CHECK_STR(birds[4].label, label47);
    /* Length counts decoded characters: 46 + "é" is 47, 47 + "é" is 48. */
    wildlife_bird_t bird;
    snprintf(text, sizeof(text), "{\"label\": \"%.46s\xC3\xA9\"}", label47);
    CHECK(parse_one(text, &bird) == 1);
    CHECK(strlen(bird.label) == 47);
    snprintf(text, sizeof(text), "{\"label\": \"%s\xC3\xA9\"}", label47);
    CHECK(parse_one(text, &bird) == 0);

    /* The photo URL must be a path on the gallery that fits. */
    char path95[96];
    char path96[97];
    memset(path95, 'p', 95);
    path95[0] = '/';
    path95[95] = '\0';
    memset(path96, 'p', 96);
    path96[0] = '/';
    path96[96] = '\0';
    photo("null", "");
    photo("\"http://x/y.jpg\"", "");
    photo("\"species_photo/x\"", "");
    photo("5", "");
    snprintf(text, sizeof(text), "\"%s\"", path95);
    photo(text, path95);
    snprintf(text, sizeof(text), "\"%s\"", path96);
    photo(text, "");
    CHECK(parse_one("{\"label\": \"Wren\"}", &bird) == 1);
    CHECK_STR(bird.photo_url, "");

    /* Missing or null time and confidence are empty, and the bird kept. */
    CHECK(parse_one("{\"label\": \"Wren\"}", &bird) == 1);
    CHECK_STR(bird.time, "");
    CHECK_STR(bird.confidence, "");
    CHECK(parse_one("{\"label\": \"Wren\", \"time\": null, \"confidence\": null}", &bird) == 1);
    CHECK_STR(bird.label, "Wren");
    CHECK_STR(bird.time, "");
    CHECK_STR(bird.confidence, "");
    CHECK(parse_one("{\"label\": \"Wren\", \"time\": 5, \"confidence\": \"100.00000%\"}", &bird) == 1);
    CHECK_STR(bird.time, "");
    CHECK_STR(bird.confidence, "");

    /* It stops after max birds. */
    snprintf(birds[2].label, sizeof(birds[2].label), "untouched");
    n = parse("{\"birds\": [{\"label\": \"A\"}, {\"label\": \"B\"}, {\"label\": \"C\"}, "
              "{\"label\": \"D\"}, {\"label\": \"E\"}]}",
              birds, 2);
    CHECK(n == 2);
    CHECK_STR(birds[0].label, "A");
    CHECK_STR(birds[1].label, "B");
    CHECK_STR(birds[2].label, "untouched");

    /* Replies without a well-formed birds array. */
    CHECK(parse("", birds, WILDLIFE_MAX_BIRDS) == -1);
    CHECK(parse("[]", birds, WILDLIFE_MAX_BIRDS) == -1);
    CHECK(parse("{}", birds, WILDLIFE_MAX_BIRDS) == -1);
    CHECK(parse("{\"birds\": {}}", birds, WILDLIFE_MAX_BIRDS) == -1);
    CHECK(parse("{\"birds\": null}", birds, WILDLIFE_MAX_BIRDS) == -1);
    CHECK(parse("{\"sighting\": {\"label\": \"Gray fox\"}}", birds, WILDLIFE_MAX_BIRDS) == -1);
    CHECK(parse("{\"birds\": [{\"label\": \"A\"} {\"label\": \"B\"}]}", birds, WILDLIFE_MAX_BIRDS) == -1);
    CHECK(parse("{\"birds\": [{\"label\": \"A\"},]}", birds, WILDLIFE_MAX_BIRDS) == -1);
    CHECK(parse("{\"birds\": [{\"label\": \"A\\x\"}]}", birds, WILDLIFE_MAX_BIRDS) == -1);
    CHECK(parse("{\"birds\": []}", birds, WILDLIFE_MAX_BIRDS) == 0);
    CHECK(parse(" { \"birds\" : [ ] } ", birds, WILDLIFE_MAX_BIRDS) == 0);
    /* Cut off anywhere before the end of the birds array. */
    size_t array_end = (size_t)(strstr(THREE_BIRDS, " ],") - THREE_BIRDS) + 2;
    int accepted_cuts = 0;
    for (size_t cut = 0; cut < array_end; ++cut) {
        if (wildlife_parse_birds(THREE_BIRDS, cut, birds, WILDLIFE_MAX_BIRDS) != -1) {
            printf("cut at %u was accepted\n", (unsigned)cut);
            ++accepted_cuts;
        }
    }
    CHECK(accepted_cuts == 0);

    /* Joining a photo path to the server's address. */
    CHECK(wildlife_join_url("http://h:8080", "/species_photo/x", out, sizeof(out)));
    CHECK_STR(out, "http://h:8080/species_photo/x");
    snprintf(out, sizeof(out), "stale");
    CHECK(!wildlife_join_url("http://h:8080", "species_photo/x", out, sizeof(out)));
    CHECK_STR(out, "");
    snprintf(out, sizeof(out), "stale");
    CHECK(!wildlife_join_url("http://h:8080", "", out, sizeof(out)));
    CHECK_STR(out, "");
    size_t joined = strlen("http://h:8080/species_photo/x");
    snprintf(out, sizeof(out), "stale");
    CHECK(!wildlife_join_url("http://h:8080", "/species_photo/x", out, joined));
    CHECK_STR(out, "");
    CHECK(wildlife_join_url("http://h:8080", "/species_photo/x", out, joined + 1));
    CHECK_STR(out, "http://h:8080/species_photo/x");

    /* Rows under "also heard". */
    heard("Common Raven", "5:21 PM", 20, "Common Raven 5:21 PM");
    heard("Chihuahuan Raven", "5:02 PM", 20, "Chihuahuan Raven");
    heard("Chestnut-backed Chickadee", "3:10 PM", 20, "Chestnut-backed Chic");
    heard("Common Raven", "", 20, "Common Raven");
    heard("Common Raven", "5:21 PM", 17, "Common Raven");
    CHECK_DONE();
}
