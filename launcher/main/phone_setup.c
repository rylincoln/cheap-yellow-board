#include "phone_setup.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "form_check.h"

#define MAX_NETWORKS 24
#define SSID_MAX 32
#define PASSWORD_MAX 64
#define URL_MAX 127   /* birdnet_viewer keeps the server in 128 bytes */
#define FORM_MAX 768  /* the three fields, percent-encoded */
#define DNS_PORT 53
#define DNS_PACKET_MAX 512

static const char *TAG = "phone_setup";

typedef struct {
    char ssid[SSID_MAX + 1];
    bool secured;
} offered_network_t;

static esp_netif_t *ap_netif;
static httpd_handle_t server;
static volatile bool dns_running;
static volatile bool dns_stopped = true;
static volatile bool saved;
static phone_setup_save_fn save_settings;
static char network_name[24];
static char address[16];
static char portal_url[32]; /* owned here: the DHCP server keeps the pointer */
static uint32_t address_bytes; /* network byte order, as in a DNS answer */
static offered_network_t networks[MAX_NETWORKS];
static int network_count;
static char saved_ssid[SSID_MAX + 1];
static char saved_url[URL_MAX + 1];

/* ---- DNS: every name is the board, so phones find the page ---- */

/* Turns a query into its answer in place: an A question gets the board's
   address, any other question an empty answer. Returns the answer's length,
   or 0 to ignore the packet. */
static int dns_answer(uint8_t *packet, int length, int capacity)
{
    /* A standard query (QR 0, opcode 0) with exactly one question. */
    if (length < 12 || (packet[2] & 0xF8) != 0 || packet[4] != 0 || packet[5] != 1) {
        return 0;
    }
    int at = 12;
    while (at < length && packet[at] != 0) {
        if ((packet[at] & 0xC0) != 0) {
            return 0; /* questions don't use name compression */
        }
        at += packet[at] + 1;
    }
    if (at + 5 > length) {
        return 0;
    }
    ++at; /* the root label */
    int type = packet[at] << 8 | packet[at + 1];
    int class = (packet[at + 2] << 8 | packet[at + 3]) & 0x7FFF;
    at += 4;
    bool answered = type == 1 && class == 1;
    if (at + (answered ? 16 : 0) > capacity) {
        return 0;
    }
    packet[2] = 0x84 | (packet[2] & 0x01); /* response, authoritative, RD kept */
    packet[3] = 0;                         /* no error */
    packet[6] = 0;
    packet[7] = answered ? 1 : 0;
    memset(packet + 8, 0, 4); /* no authority or additional records */
    if (answered) {
        /* Name: pointer to the question; type A, class IN, TTL 60 s, 4 bytes. */
        static const uint8_t record[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4};
        memcpy(packet + at, record, sizeof(record));
        memcpy(packet + at + sizeof(record), &address_bytes, 4);
        at += sizeof(record) + 4;
    }
    return at;
}

static void dns_task(void *argument)
{
    (void)argument;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in local = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = address_bytes,
    };
    /* A short timeout, so the loop notices phone_setup_stop. */
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 250 * 1000};
    if (sock < 0 || bind(sock, (struct sockaddr *)&local, sizeof(local)) != 0 ||
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        ESP_LOGE(TAG, "DNS server could not start");
        dns_running = false;
    }
    uint8_t packet[DNS_PACKET_MAX];
    while (dns_running) {
        struct sockaddr_in from;
        socklen_t from_length = sizeof(from);
        int length =
            recvfrom(sock, packet, sizeof(packet), 0, (struct sockaddr *)&from, &from_length);
        int reply = length > 0 ? dns_answer(packet, length, sizeof(packet)) : 0;
        if (reply > 0) {
            sendto(sock, packet, reply, 0, (struct sockaddr *)&from, from_length);
        }
    }
    if (sock >= 0) {
        close(sock);
    }
    dns_stopped = true;
    vTaskDelete(NULL);
}

/* ---- The page ---- */

static const char PAGE_HEAD[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Board setup</title><style>"
    "body{font:16px/1.4 -apple-system,system-ui,sans-serif;margin:0;padding:20px;"
    "background:#f6f7f9;color:#111}"
    "main{max-width:420px;margin:auto}h1{font-size:22px;margin:0 0 4px}"
    "p{margin:0 0 14px;color:#445}small{color:#667}"
    "label{display:block;font-size:13px;color:#445;margin:16px 0 4px}"
    "input{box-sizing:border-box;width:100%;font-size:17px;padding:10px;"
    "border:1px solid #c9ced6;border-radius:8px;background:#fff;color:#111}"
    ".nets{margin-bottom:8px}"
    ".nets button{display:block;width:100%;text-align:left;font-size:16px;padding:10px;"
    "margin:0 0 -1px;border:1px solid #c9ced6;border-radius:0;background:#fff;color:#111}"
    ".nets button:first-child{border-radius:8px 8px 0 0}"
    ".nets button:last-child{border-radius:0 0 8px 8px}"
    ".show{display:flex;gap:8px;align-items:center;font-size:14px;margin:8px 0 2px}"
    ".show input{width:auto}"
    ".go{margin-top:22px;width:100%;font-size:17px;font-weight:600;padding:13px;border:0;"
    "border-radius:10px;background:#1f6f43;color:#fff}"
    ".err{color:#b00020;font-weight:600}"
    "</style></head><body><main>";

/* Sends text with the characters HTML gives meaning to escaped. */
static void send_escaped(httpd_req_t *request, const char *text)
{
    char buffer[64];
    size_t used = 0;
    for (; *text != '\0'; ++text) {
        const char *entity = *text == '&'    ? "&amp;"
                             : *text == '<'  ? "&lt;"
                             : *text == '>'  ? "&gt;"
                             : *text == '"'  ? "&quot;"
                             : *text == '\'' ? "&#39;"
                                             : NULL;
        size_t size = entity != NULL ? strlen(entity) : 1;
        if (used + size > sizeof(buffer)) {
            httpd_resp_send_chunk(request, buffer, used);
            used = 0;
        }
        if (entity != NULL) {
            memcpy(buffer + used, entity, size);
        } else {
            buffer[used] = *text;
        }
        used += size;
    }
    if (used > 0) {
        httpd_resp_send_chunk(request, buffer, used);
    }
}

static void begin_page(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(request, PAGE_HEAD);
}

/* The form, with an error above it when a submission was refused. The saved
   password is never sent: anyone near the open access point could read it. */
static void send_form(httpd_req_t *request, const char *error, const char *ssid,
                      const char *url)
{
    begin_page(request);
    httpd_resp_sendstr_chunk(request,
                             "<h1>Board setup</h1><p>Connect the board to your Wi-Fi.</p>");
    if (error != NULL) {
        httpd_resp_sendstr_chunk(request, "<p class=\"err\">");
        send_escaped(request, error);
        httpd_resp_sendstr_chunk(request, "</p>");
    }
    httpd_resp_sendstr_chunk(request,
                             "<form method=\"post\" action=\"/save\"><label>Network</label>");
    if (network_count > 0) {
        httpd_resp_sendstr_chunk(request, "<div class=\"nets\">");
        for (int i = 0; i < network_count; ++i) {
            httpd_resp_sendstr_chunk(
                request,
                "<button type=\"button\" onclick=\"this.form.ssid.value=this.value\" value=\"");
            send_escaped(request, networks[i].ssid);
            httpd_resp_sendstr_chunk(request, "\">");
            send_escaped(request, networks[i].ssid);
            httpd_resp_sendstr_chunk(request,
                                     networks[i].secured ? " &#128274;</button>" : "</button>");
        }
        httpd_resp_sendstr_chunk(request, "</div>");
    }
    httpd_resp_sendstr_chunk(request,
                             "<input name=\"ssid\" maxlength=\"32\" required "
                             "autocapitalize=\"none\" autocorrect=\"off\" spellcheck=\"false\" "
                             "placeholder=\"Network name\" value=\"");
    send_escaped(request, ssid);
    httpd_resp_sendstr_chunk(
        request,
        "\"><label>Password</label>"
        "<input name=\"pass\" type=\"password\" maxlength=\"64\" autocapitalize=\"none\" "
        "autocorrect=\"off\" spellcheck=\"false\">"
        "<label class=\"show\"><input type=\"checkbox\" "
        "onclick=\"this.form.pass.type=this.checked?'text':'password'\">Show password</label>"
        "<small>Leave it blank for an open network. For the network already saved, "
        "blank keeps its password.</small>"
        "<label>BirdNET server <small>for the BirdNET viewer</small></label>"
        "<input name=\"url\" maxlength=\"127\" inputmode=\"url\" autocapitalize=\"none\" "
        "autocorrect=\"off\" spellcheck=\"false\" placeholder=\"http://birdnet.local\" "
        "value=\"");
    send_escaped(request, url);
    httpd_resp_sendstr_chunk(request, "\"><button class=\"go\">Save and connect</button>"
                                      "</form></main></body></html>");
    httpd_resp_sendstr_chunk(request, NULL);
}

static esp_err_t page_get(httpd_req_t *request)
{
    send_form(request, NULL, saved_ssid, saved_url);
    return ESP_OK;
}

/* Decodes a form value in place; false if it would hold a NUL. */
static bool form_decode(char *text)
{
    char *out = text;
    for (const char *in = text; *in != '\0'; ++in) {
        if (*in == '+') {
            *out++ = ' ';
        } else if (*in == '%' && isxdigit((unsigned char)in[1]) &&
                   isxdigit((unsigned char)in[2])) {
            char hex[3] = {in[1], in[2], '\0'};
            char decoded = (char)strtol(hex, NULL, 16);
            if (decoded == '\0') {
                return false;
            }
            *out++ = decoded;
            in += 2;
        } else {
            *out++ = *in;
        }
    }
    *out = '\0';
    return true;
}

/* Copies one decoded field of the form; a missing field is empty. False if
   it is too long for size or holds a NUL. */
static bool form_field(const char *form, const char *key, char *value, size_t size)
{
    char encoded[FORM_MAX];
    esp_err_t result = httpd_query_key_value(form, key, encoded, sizeof(encoded));
    value[0] = '\0';
    if (result == ESP_ERR_NOT_FOUND) {
        return true;
    }
    if (result != ESP_OK || !form_decode(encoded) || strlen(encoded) >= size) {
        return false;
    }
    memcpy(value, encoded, strlen(encoded) + 1);
    return true;
}

static esp_err_t save_post(httpd_req_t *request)
{
    char form[FORM_MAX];
    if (request->content_len >= sizeof(form)) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Form too long");
        return ESP_FAIL;
    }
    size_t received = 0;
    while (received < request->content_len) {
        int got = httpd_req_recv(request, form + received, request->content_len - received);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (got <= 0) {
            return ESP_FAIL;
        }
        received += got;
    }
    form[received] = '\0';

    char ssid[SSID_MAX + 1];
    char password[PASSWORD_MAX + 1];
    char url[URL_MAX + 1];
    bool ssid_ok = form_field(form, "ssid", ssid, sizeof(ssid));
    bool password_ok = form_field(form, "pass", password, sizeof(password)) &&
                       form_password_valid(password);
    bool url_ok = form_field(form, "url", url, sizeof(url)) && form_url_valid(url, sizeof(url));
    const char *error = !ssid_ok          ? "The network name is too long."
                        : ssid[0] == '\0' ? "Choose or type a network."
                        : !password_ok    ? "The password must be 8 to 63 characters."
                        : !url_ok         ? "The BirdNET server must be an http:// address."
                                          : NULL;
    if (error == NULL && !save_settings(ssid, password, url)) {
        error = "The board could not save that. Try again.";
    }
    if (error != NULL) {
        send_form(request, error, ssid, url);
        return ESP_OK;
    }

    begin_page(request);
    httpd_resp_sendstr_chunk(request, "<h1>Saved</h1><p>The board is joining <b>");
    send_escaped(request, ssid);
    httpd_resp_sendstr_chunk(request, "</b> now. Your phone will leave ");
    send_escaped(request, network_name);
    httpd_resp_sendstr_chunk(request, " in a moment; that's expected.</p>"
                                      "<p>You can close this page.</p></main></body></html>");
    httpd_resp_sendstr_chunk(request, NULL);
    saved = true;
    ESP_LOGI(TAG, "A phone saved Wi-Fi network %s", ssid);
    return ESP_OK;
}

/* Phones probe a known address when they join a network, such as
   /generate_204 or /hotspot-detect.html. Sending every unknown path to the
   page makes them open it as a sign-in page. */
static esp_err_t redirect_to_page(httpd_req_t *request, httpd_err_code_t error)
{
    (void)error;
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", portal_url);
    /* iOS opens its sign-in sheet only for a redirect with a body. */
    httpd_resp_sendstr(request, "Board setup");
    return ESP_OK;
}

/* ---- Starting and stopping ---- */

/* Creates the access point's interface once, with the page's address in
   the DHCP lease (RFC 8910), which newer phones open directly. */
static esp_err_t create_ap_netif(void)
{
    if (ap_netif != NULL) {
        return ESP_OK;
    }
    ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        return ESP_FAIL;
    }
    esp_netif_ip_info_t ip_info;
    esp_err_t result = esp_netif_get_ip_info(ap_netif, &ip_info);
    if (result != ESP_OK) {
        return result;
    }
    address_bytes = ip_info.ip.addr;
    snprintf(address, sizeof(address), IPSTR, IP2STR(&ip_info.ip));
    snprintf(portal_url, sizeof(portal_url), "http://%s/", address);
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, portal_url,
                           strlen(portal_url));
    return esp_netif_dhcps_start(ap_netif);
}

static void copy_config(const phone_setup_config_t *config)
{
    network_count = 0;
    for (int i = 0; i < config->network_count && network_count < MAX_NETWORKS; ++i) {
        const wifi_ap_record_t *ap = &config->networks[i];
        if (ap->ssid[0] == '\0') {
            continue; /* hidden: its name has to be typed */
        }
        offered_network_t *offered = &networks[network_count++];
        memcpy(offered->ssid, ap->ssid, SSID_MAX);
        offered->ssid[SSID_MAX] = '\0';
        offered->secured = ap->authmode != WIFI_AUTH_OPEN;
    }
    strlcpy(saved_ssid, config->ssid, sizeof(saved_ssid));
    strlcpy(saved_url, config->birdnet_url, sizeof(saved_url));
    save_settings = config->save;
    saved = false;
}

static esp_err_t start_access_point(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(network_name, sizeof(network_name), "CYD-Setup-%02X%02X", mac[4], mac[5]);
    wifi_config_t ap_config = {
        .ap = {
            .channel = 1, /* the station's channel instead while it is joined */
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = 2,
        },
    };
    size_t length = strlen(network_name);
    memcpy(ap_config.ap.ssid, network_name, length);
    ap_config.ap.ssid_len = length;
    esp_err_t result = esp_wifi_set_mode(WIFI_MODE_APSTA);
    return result == ESP_OK ? esp_wifi_set_config(WIFI_IF_AP, &ap_config) : result;
}

static esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* With the DNS socket and the launcher's own downloads, within lwIP's
       10 sockets. Phones open several connections, so reuse the oldest. */
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.stack_size = 6144;
    esp_err_t result = httpd_start(&server, &config);
    if (result != ESP_OK) {
        server = NULL;
        return result;
    }
    const httpd_uri_t page = {.uri = "/", .method = HTTP_GET, .handler = page_get};
    const httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_post};
    httpd_register_uri_handler(server, &page);
    httpd_register_uri_handler(server, &save);
    return httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirect_to_page);
}

esp_err_t phone_setup_start(const phone_setup_config_t *config)
{
    if (server != NULL || !dns_stopped) {
        return ESP_ERR_INVALID_STATE;
    }
    copy_config(config);
    esp_err_t result = create_ap_netif();
    if (result == ESP_OK) {
        result = start_access_point();
    }
    if (result == ESP_OK) {
        dns_running = true;
        dns_stopped = false;
        if (xTaskCreate(dns_task, "dns", 3072, NULL, 4, NULL) != pdPASS) {
            dns_running = false;
            dns_stopped = true;
            result = ESP_ERR_NO_MEM;
        }
    }
    if (result == ESP_OK) {
        result = start_web_server();
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Phone setup could not start: %s", esp_err_to_name(result));
        phone_setup_stop();
        return result;
    }
    ESP_LOGI(TAG, "Phone setup on Wi-Fi %s at %s", network_name, portal_url);
    return ESP_OK;
}

void phone_setup_stop(void)
{
    if (server != NULL) {
        httpd_stop(server);
        server = NULL;
    }
    dns_running = false;
    for (int i = 0; i < 20 && !dns_stopped; ++i) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
}

const char *phone_setup_network_name(void)
{
    return network_name;
}

const char *phone_setup_address(void)
{
    return address;
}

int phone_setup_phone_count(void)
{
    wifi_sta_list_t list;
    return server != NULL && esp_wifi_ap_get_sta_list(&list) == ESP_OK ? list.num : 0;
}

bool phone_setup_saved(void)
{
    return saved;
}
