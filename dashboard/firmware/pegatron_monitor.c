#include "pegatron_monitor.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"
#include "hardware/flash.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/rand.h"
#include "pico/time.h"

static pegatron_settings settings;
static mqtt_client_t *client;
static ip_addr_t broker;
static char availability_topic[80], telemetry_topic[80], event_topic[80], ack_topic[80], command_topic[80], wifi_topic[80], wifi_result_topic[80];
static char boot_id[17], link_id[17], will[100];
static uint64_t seq, event_seq, wifi_retry, mqtt_retry, next_publish;
static bool connecting, announce, online, dirty, reported_faults[4];
static const char *const modes[] = {"idle", "sorting", "maintenance", "fault"};
static const char *const fault_codes[] = {"HOME_TIMEOUT_1", "HOME_TIMEOUT_2",
                                          "TOF_UNAVAILABLE_1", "TOF_UNAVAILABLE_2"};
#define COMMAND_MAX_AGE_MS 5000
static char command_buffer[512], recent_ids[4][65];
static size_t command_size;
static bool command_topic_ok, command_overflow, incoming_wifi;
static unsigned sub_stage;
static unsigned recent_next;
static uint64_t now_ms;
static unsigned reported_mode = sizeof modes / sizeof modes[0];

static const char *flag(pegatron_flag value) {
    return value == PEGATRON_TRUE ? "true" : value == PEGATRON_FALSE ? "false" : "null";
}

static const char *number(char text[24], bool known, int64_t value) {
    if (!known) return "null";
    snprintf(text, 24, "%" PRId64, value);
    return text;
}

static void station_json(char text[180], const pegatron_station *station) {
    char height[24], distance[24], reference[24];
    snprintf(text, 180,
             "{\"height_mm\":%s,\"distance_mm\":%s,\"sample_valid\":%s,\"home\":%s,"
             "\"push_count\":%" PRIu32 ",\"reference_mm\":%s}",
             number(height, station->height_known, station->height_mm),
             number(distance, station->distance_known, station->distance_mm),
             station->sample_valid ? "true" : "false", flag(station->home), station->push_count,
             number(reference, station->reference_known, station->reference_mm));
}

static void permission_json(char text[80], const char *deny) {
    if (deny) snprintf(text, 80, "{\"allowed\":false,\"reason\":\"%s\"}", deny);
    else snprintf(text, 80, "{\"allowed\":true,\"reason\":\"OK\"}");
}

static void permissions_json(char text[520], const pegatron_snapshot *snapshot) {
    char p[6][80];
    for (unsigned i = 0; i < 6; ++i) permission_json(p[i], snapshot->deny[i]);
    snprintf(text, 520,
             "{\"ENABLE_SORTING\":%s,\"PAUSE_SORTING\":%s,"
             "\"TEST_PUSHER\":{\"station_1\":%s,\"station_2\":%s},"
             "\"CALIBRATE_TOF\":{\"station_1\":%s,\"station_2\":%s}}",
             p[0], p[1], p[2], p[3], p[4], p[5]);
}

static bool fault_active(const pegatron_snapshot *snapshot, unsigned i) {
    return i < 2 ? snapshot->home_timeout[i] : snapshot->tof_unavailable[i - 2];
}

static bool publish_snapshot(const pegatron_snapshot *snapshot, uint64_t uptime_ms) {
    const unsigned mode = (unsigned)snapshot->mode;
    if (mode >= sizeof modes / sizeof modes[0]) return false;

    char payload[1400], permissions[520], first[180], second[180], s[24], m[24], l[24], faults[128] = "";
    size_t used = 0;
    for (unsigned i = 0; i < sizeof fault_codes / sizeof fault_codes[0]; ++i) {
        if (!fault_active(snapshot, i)) continue;
        used += (size_t)snprintf(faults + used, sizeof faults - used, "%s\"%s\"", used ? "," : "", fault_codes[i]);
    }
    permissions_json(permissions, snapshot);
    station_json(first, &snapshot->station[0]);
    station_json(second, &snapshot->station[1]);
    const int length = snprintf(payload, sizeof payload,
        "{\"schema_version\":1,\"boot_id\":\"%s\",\"link_id\":\"%s\",\"seq\":%" PRIu64
        ",\"uptime_ms\":%" PRIu64 ",\"control_epoch\":0,\"mode\":\"%s\",\"estop\":%s,"
        "\"faults\":[%s],\"station_1\":%s,\"station_2\":%s,"
        "\"trays\":{\"S\":%s,\"M\":%s,\"L\":%s},\"permissions\":%s}",
        boot_id, link_id, seq, uptime_ms, modes[mode], flag(snapshot->estop),
        faults, first, second,
        number(s, snapshot->tray_confirmed[0], snapshot->tray_count[0]),
        number(m, snapshot->tray_confirmed[1], snapshot->tray_count[1]),
        number(l, snapshot->tray_confirmed[2], snapshot->tray_count[2]), permissions);

    if (length < 0 || (size_t)length >= sizeof payload) return false;
    const err_t result = mqtt_publish(client, telemetry_topic, payload, (u16_t)length, 0, 0, NULL, NULL);
    if (result != ERR_OK) return false;
    ++seq;
    return true;
}

static bool publish_event(const char *kind, const char *message, const char *station) {
    char payload[256];
    const int length = snprintf(payload, sizeof payload,
        "{\"schema_version\":1,\"boot_id\":\"%s\",\"event_id\":\"%" PRIu64 "\","
        "\"kind\":\"%s\",\"message\":\"%s\",\"station\":%s}", boot_id, event_seq, kind, message, station);
    if (length < 0 || (size_t)length >= sizeof payload ||
        mqtt_publish(client, event_topic, payload, (u16_t)length, 1, 0, NULL, NULL) != ERR_OK) return false;
    ++event_seq;
    return true;
}

static void publish_changes(const pegatron_snapshot *snapshot) {
    for (unsigned i = 0; i < sizeof fault_codes / sizeof fault_codes[0]; ++i) {
        const bool active = fault_active(snapshot, i);
        if (reported_faults[i] == active) continue;
        const char *kind = active ? "fault_raised" : "fault_cleared";
        if (publish_event(kind, fault_codes[i], i % 2 ? "2" : "1"))
            reported_faults[i] = active;
    }
    const unsigned mode = (unsigned)snapshot->mode;
    if (mode < sizeof modes / sizeof modes[0] && mode != reported_mode &&
        publish_event("mode_changed", modes[mode], "null"))
        reported_mode = mode;
}

static bool token_ok(const char *text);

// ---------------------------------------------------------------------------
// Wi-Fi provisioning from the dashboard: scan, switch network, keep it in flash.
// A switch is only committed once MQTT is online on the new network; otherwise the
// previous network is restored so the device is never stranded by a typo.
// ---------------------------------------------------------------------------
#define CRED_MAGIC 0x50474D31u
#define CRED_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define MAX_NETS 12
#define SWITCH_TIMEOUT_MS 25000

typedef struct { uint32_t magic; char ssid[33]; char pass[65]; } credentials_blob;
typedef struct { char ssid[33]; int16_t rssi; bool secure; } scan_entry;
enum { W_IDLE, W_SCAN, W_SWITCH, W_REVERT };

static char cur_ssid[33], cur_pass[65], prev_ssid[33], prev_pass[65];
static char wifi_request_id[65], wifi_fail[24];
static unsigned wifi_state;
static uint64_t wifi_deadline;
static scan_entry nets[MAX_NETS];
static unsigned net_count;

static void flash_write_page(void *page) {
    flash_range_erase(CRED_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CRED_OFFSET, (const uint8_t *)page, FLASH_PAGE_SIZE);
}

static void save_credentials(void) {
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof page);
    credentials_blob blob = {.magic = CRED_MAGIC};
    snprintf(blob.ssid, sizeof blob.ssid, "%s", cur_ssid);
    snprintf(blob.pass, sizeof blob.pass, "%s", cur_pass);
    memcpy(page, &blob, sizeof blob);
    // Pauses the control core for a few tens of ms while flash is erased.
    flash_safe_execute(flash_write_page, page, 1000);
}

static void load_credentials(void) {
    credentials_blob blob;
    memcpy(&blob, (const void *)(XIP_BASE + CRED_OFFSET), sizeof blob);
    if (blob.magic != CRED_MAGIC || memchr(blob.ssid, 0, sizeof blob.ssid) == NULL ||
        memchr(blob.pass, 0, sizeof blob.pass) == NULL || !blob.ssid[0]) return;
    memcpy(cur_ssid, blob.ssid, sizeof cur_ssid);
    memcpy(cur_pass, blob.pass, sizeof cur_pass);
}

static void json_escape(char *out, size_t size, const char *text) {
    size_t n = 0;
    for (; *text && n + 3 < size; ++text) {
        if (*text == '"' || *text == '\\') out[n++] = '\\';
        out[n++] = *text;
    }
    out[n] = '\0';
}

static int scan_result(void *env, const cyw43_ev_scan_result_t *r) {
    (void)env;
    if (!r || !r->ssid_len || r->ssid_len > 32) return 0;
    scan_entry entry = {.rssi = r->rssi, .secure = r->auth_mode != 0};
    memcpy(entry.ssid, r->ssid, r->ssid_len);
    for (unsigned i = 0; i < r->ssid_len; ++i)
        if ((uint8_t)entry.ssid[i] < 0x20 || entry.ssid[i] == 0x7F) return 0;
    unsigned weakest = 0;
    for (unsigned i = 0; i < net_count; ++i) {
        if (!strcmp(nets[i].ssid, entry.ssid)) {
            if (entry.rssi > nets[i].rssi) nets[i] = entry;
            return 0;
        }
        if (nets[i].rssi < nets[weakest].rssi) weakest = i;
    }
    if (net_count < MAX_NETS) nets[net_count++] = entry;
    else if (entry.rssi > nets[weakest].rssi) nets[weakest] = entry;
    return 0;
}

static bool publish_wifi_result(const char *id, const char *kind, bool ok, const char *reason, bool with_networks) {
    char payload[1600];
    int length = snprintf(payload, sizeof payload,
        "{\"schema_version\":1,\"request_id\":\"%s\",\"kind\":\"%s\",\"ok\":%s,\"reason\":\"%s\",\"ssid\":",
        id, kind, ok ? "true" : "false", reason);
    if (!strcmp(kind, "connect")) {
        char name[80];
        json_escape(name, sizeof name, cur_ssid);
        length += snprintf(payload + length, sizeof payload - length, "\"%s\"", name);
    } else length += snprintf(payload + length, sizeof payload - length, "null");
    length += snprintf(payload + length, sizeof payload - length, ",\"networks\":[");
    for (unsigned i = 0; with_networks && i < net_count && length < (int)sizeof payload - 130; ++i) {
        char name[80];
        json_escape(name, sizeof name, nets[i].ssid);
        length += snprintf(payload + length, sizeof payload - length,
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}", i ? "," : "", name, nets[i].rssi,
            nets[i].secure ? "true" : "false");
    }
    length += snprintf(payload + length, sizeof payload - length, "]}");
    if (length < 0 || (size_t)length >= sizeof payload) return false;
    return mqtt_publish(client, wifi_result_topic, payload, (u16_t)length, 1, 0, NULL, NULL) == ERR_OK;
}

static void restart_link(void) {
    if (online || announce || connecting) mqtt_disconnect(client);
    online = announce = connecting = false;
    sub_stage = 0;
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    wifi_retry = 0;
    mqtt_retry = now_ms + 1500;  // Let the old link drop before MQTT tries again.
}

static void wifi_scan_request(const char *id) {
    if (wifi_state != W_IDLE) { publish_wifi_result(id, "scan", false, "BUSY", false); return; }
    cyw43_wifi_scan_options_t options = {0};
    net_count = 0;
    if (cyw43_wifi_scan(&cyw43_state, &options, NULL, scan_result) != 0) {
        publish_wifi_result(id, "scan", false, "SCAN_FAILED", false);
        return;
    }
    snprintf(wifi_request_id, sizeof wifi_request_id, "%s", id);
    wifi_state = W_SCAN;
    wifi_deadline = now_ms + 10000;
}

static void wifi_connect_request(const char *id, const char *ssid, const char *pass) {
    const size_t ssid_len = strlen(ssid), pass_len = strlen(pass);
    if (wifi_state != W_IDLE) { publish_wifi_result(id, "connect", false, "BUSY", false); return; }
    if (!ssid_len || ssid_len > 32 || (pass_len && (pass_len < 8 || pass_len > 63))) {
        publish_wifi_result(id, "connect", false, "BAD_CREDENTIALS", false);
        return;
    }
    memcpy(prev_ssid, cur_ssid, sizeof prev_ssid);
    memcpy(prev_pass, cur_pass, sizeof prev_pass);
    snprintf(cur_ssid, sizeof cur_ssid, "%s", ssid);
    snprintf(cur_pass, sizeof cur_pass, "%s", pass);
    snprintf(wifi_request_id, sizeof wifi_request_id, "%s", id);
    wifi_state = W_SWITCH;
    wifi_deadline = now_ms + SWITCH_TIMEOUT_MS;
    restart_link();
}

static void wifi_service(uint64_t now) {
    switch (wifi_state) {
    case W_SCAN:
        if (cyw43_wifi_scan_active(&cyw43_state) && now < wifi_deadline) break;
        if (!online || publish_wifi_result(wifi_request_id, "scan", true, "OK", true)) wifi_state = W_IDLE;
        break;
    case W_SWITCH:
        if (online) {
            if (publish_wifi_result(wifi_request_id, "connect", true, "OK", false)) {
                save_credentials();
                wifi_state = W_IDLE;
            }
        } else if (now >= wifi_deadline) {
            const bool joined = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
            snprintf(wifi_fail, sizeof wifi_fail, "%s", joined ? "BROKER_UNREACHABLE" : "WIFI_FAILED");
            memcpy(cur_ssid, prev_ssid, sizeof cur_ssid);
            memcpy(cur_pass, prev_pass, sizeof cur_pass);
            wifi_state = W_REVERT;
            restart_link();
        }
        break;
    case W_REVERT:  // No deadline: keep trying the old network and report once back online.
        if (online) {
            if (publish_wifi_result(wifi_request_id, "connect", false, wifi_fail, false)) wifi_state = W_IDLE;
        }
        break;
    default: break;
    }
}

// Reads a JSON string value, undoing \" and \\ only. Other escapes are refused.
static bool json_text(const char *json, const char *key, char *out, size_t size, bool allow_empty) {
    char needle[32];
    snprintf(needle, sizeof needle, "\"%s\":\"", key);
    const char *at = strstr(json, needle);
    if (!at) return false;
    at += strlen(needle);
    size_t n = 0;
    for (; *at && *at != '"'; ++at) {
        if (*at == '\\') {
            ++at;
            if (*at != '"' && *at != '\\') return false;
        }
        if (n + 1 >= size) return false;
        out[n++] = *at;
    }
    out[n] = '\0';
    return *at == '"' && (n || allow_empty);
}

static void handle_wifi(const char *json) {
    char id[65], action[12], ssid[33], pass[65];
    if (strstr(json, "\"schema_version\":1") == NULL || !json_text(json, "request_id", id, sizeof id, false) ||
        !token_ok(id) || !json_text(json, "action", action, sizeof action, false)) return;
    for (unsigned i = 0; i < 4; ++i)
        if (!strcmp(recent_ids[i], id)) return;
    strcpy(recent_ids[recent_next++ % 4], id);
    if (!strcmp(action, "scan")) return wifi_scan_request(id);
    if (strcmp(action, "connect") || !json_text(json, "ssid", ssid, sizeof ssid, false) ||
        !json_text(json, "password", pass, sizeof pass, true))
        return (void)publish_wifi_result(id, "connect", false, "BAD_CREDENTIALS", false);
    wifi_connect_request(id, ssid, pass);
}

// Reads a flat JSON value made by pydantic (compact, no nesting). Returns false if absent.
static bool json_field(const char *json, const char *key, char *out, size_t size) {
    char needle[32];
    snprintf(needle, sizeof needle, "\"%s\":", key);
    const char *at = strstr(json, needle);
    if (!at) return false;
    at += strlen(needle);
    const bool quoted = *at == '"';
    if (quoted) ++at;
    size_t n = 0;
    while (at[n] && at[n] != (quoted ? '"' : ',') && at[n] != '}') ++n;
    if (!n || n >= size) return false;
    memcpy(out, at, n);
    out[n] = '\0';
    return true;
}

static void send_ack(const char *request_id, bool executed, const char *reason, const char *mode) {
    char payload[300];
    const int length = snprintf(payload, sizeof payload,
        "{\"schema_version\":1,\"request_id\":\"%s\",\"link_id\":\"%s\",\"result\":\"%s\","
        "\"reason\":\"%s\",\"mode\":\"%s\"}", request_id, link_id,
        executed ? "executed" : "rejected", reason, mode);
    if (length > 0 && (size_t)length < sizeof payload)
        mqtt_publish(client, ack_topic, payload, (u16_t)length, 1, 0, NULL, NULL);
}

static bool token_ok(const char *text) {
    const size_t n = strlen(text);
    return n && n <= 64 && strspn(text, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == n;
}

// Checks link, epoch, age and replay before the control core ever sees the command.
static void handle_command(const char *json) {
    char id[65], action[24], link[24], num[24], schema[8];
    if (!json_field(json, "schema_version", schema, sizeof schema) || strcmp(schema, "1") ||
        !json_field(json, "request_id", id, sizeof id) || !token_ok(id) ||
        !json_field(json, "action", action, sizeof action)) return;  // Unparseable: no id to answer.
    pegatron_command command = {.station = 0};
    strcpy(command.request_id, id);
    const char *mode_name = "idle";
    if (!strcmp(action, "ENABLE_SORTING")) command.action = PEGATRON_ENABLE;
    else if (!strcmp(action, "PAUSE_SORTING")) command.action = PEGATRON_PAUSE;
    else if (!strcmp(action, "TEST_PUSHER")) command.action = PEGATRON_TEST;
    else if (!strcmp(action, "CALIBRATE_TOF")) command.action = PEGATRON_CALIBRATE;
    else return send_ack(id, false, "BAD_ACTION", mode_name);

    if (!json_field(json, "link_id", link, sizeof link) || strcmp(link, link_id))
        return send_ack(id, false, "LINK_MISMATCH", mode_name);
    uint64_t epoch, observed;
    if (!json_field(json, "control_epoch", num, sizeof num) || sscanf(num, "%" SCNu64, &epoch) != 1 || epoch != 0)
        return send_ack(id, false, "EPOCH_MISMATCH", mode_name);
    if (!json_field(json, "observed_uptime_ms", num, sizeof num) || sscanf(num, "%" SCNu64, &observed) != 1 ||
        observed > now_ms || now_ms - observed > COMMAND_MAX_AGE_MS)
        return send_ack(id, false, "STALE_COMMAND", mode_name);
    if (command.action == PEGATRON_TEST || command.action == PEGATRON_CALIBRATE) {
        if (!json_field(json, "station", num, sizeof num) || (num[0] != '1' && num[0] != '2') || num[1])
            return send_ack(id, false, "BAD_STATION", mode_name);
        command.station = (uint8_t)(num[0] - '0');
    }
    for (unsigned i = 0; i < 4; ++i)
        if (!strcmp(recent_ids[i], id)) return;  // Replay: already answered once.
    strcpy(recent_ids[recent_next++ % 4], id);
    if (!queue_try_add(settings.commands, &command)) send_ack(id, false, "BUSY", mode_name);
}

static void incoming_topic(void *arg, const char *topic, u32_t total) {
    (void)arg;
    incoming_wifi = !strcmp(topic, wifi_topic);
    command_topic_ok = incoming_wifi || !strcmp(topic, command_topic);
    command_overflow = total >= sizeof command_buffer;
    command_size = 0;
}

static void incoming_data(void *arg, const u8_t *data, u16_t length, u8_t flags) {
    (void)arg;
    if (!command_topic_ok) return;
    if (command_size + length < sizeof command_buffer) {
        memcpy(command_buffer + command_size, data, length);
        command_size += length;
    } else command_overflow = true;
    if (flags & MQTT_DATA_FLAG_LAST) {
        command_buffer[command_size] = '\0';
        if (!command_overflow) (incoming_wifi ? handle_wifi : handle_command)(command_buffer);
        command_topic_ok = false;
    }
}

static void subscribe_done(void *arg, err_t result) {
    (void)arg;
    if (result == ERR_OK) ++sub_stage;
}

static void connection_changed(mqtt_client_t *connection, void *arg, mqtt_connection_status_t status) {
    (void)connection;
    (void)arg;
    connecting = online = false;
    sub_stage = 0;
    announce = status == MQTT_CONNECT_ACCEPTED;
}

bool pegatron_monitor_init(const pegatron_settings *config) {
    if (client || !config || !config->device_id || !config->ssid || !config->wifi_password ||
        !config->broker_ipv4 || !config->commands || !config->acks || !config->mqtt_user || !config->mqtt_password || !config->mqtt_port) return false;
    const size_t size = strlen(config->device_id);
    if (!size || size > 48 || strspn(config->device_id,
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != size ||
        !ipaddr_aton(config->broker_ipv4, &broker) || !IP_IS_V4(&broker)) return false;

    if (cyw43_arch_init() != 0) return false;
    cyw43_arch_enable_sta_mode();
    client = mqtt_client_new();
    if (!client) {
        cyw43_arch_deinit();
        return false;
    }
    settings = *config;
    snprintf(boot_id, sizeof boot_id, "%016" PRIx64, get_rand_64());
    snprintf(availability_topic, sizeof availability_topic, "pegatron/v1/%s/availability", settings.device_id);
    snprintf(telemetry_topic, sizeof telemetry_topic, "pegatron/v1/%s/telemetry", settings.device_id);
    snprintf(event_topic, sizeof event_topic, "pegatron/v1/%s/events", settings.device_id);
    snprintf(ack_topic, sizeof ack_topic, "pegatron/v1/%s/ack", settings.device_id);
    snprintf(command_topic, sizeof command_topic, "pegatron/v1/%s/command", settings.device_id);
    snprintf(wifi_topic, sizeof wifi_topic, "pegatron/v1/%s/wifi/request", settings.device_id);
    snprintf(wifi_result_topic, sizeof wifi_result_topic, "pegatron/v1/%s/wifi/result", settings.device_id);
    snprintf(cur_ssid, sizeof cur_ssid, "%s", config->ssid);
    snprintf(cur_pass, sizeof cur_pass, "%s", config->wifi_password);
    load_credentials();  // A network chosen on the dashboard wins over the build-time one.
    settings.ssid = cur_ssid;
    settings.wifi_password = cur_pass;
    flash_safe_execute_core_init();
    mqtt_set_inpub_callback(client, incoming_topic, incoming_data, NULL);
    return true;
}

void pegatron_monitor_tick(const pegatron_snapshot *snapshot, bool changed) {
    if (!client || !snapshot) return;
    dirty |= changed;
    cyw43_arch_poll();
    now_ms = to_ms_since_boot(get_absolute_time());
    wifi_service(now_ms);
    const uint64_t now = to_ms_since_boot(get_absolute_time());
    now_ms = now;
    const int wifi = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (wifi != CYW43_LINK_UP) {
        if (online || announce || connecting) mqtt_disconnect(client);
        online = announce = connecting = false;
        if (wifi <= CYW43_LINK_DOWN && now >= wifi_retry) {
            wifi_retry = now + 3000;
            cyw43_arch_wifi_connect_async(settings.ssid, settings.wifi_password,
                                          settings.wifi_password[0] ? CYW43_AUTH_WPA2_MIXED_PSK : CYW43_AUTH_OPEN);
        }
        return;
    }

    if (!online && !announce && !connecting && now >= mqtt_retry) {
        mqtt_retry = now + 3000;
        // Keep the will immutable during an active MQTT connection attempt.
        snprintf(link_id, sizeof link_id, "%016" PRIx64, get_rand_64());
        snprintf(will, sizeof will, "{\"schema_version\":1,\"link_id\":\"%s\",\"status\":\"offline\"}", link_id);
        const struct mqtt_connect_client_info_t info = {
            .client_id = settings.device_id, .client_user = settings.mqtt_user, .client_pass = settings.mqtt_password,
            .keep_alive = 10, .will_topic = availability_topic, .will_msg = will, .will_qos = 1, .will_retain = 1
        };
        connecting = true;
        if (mqtt_client_connect(client, &broker, settings.mqtt_port, connection_changed, NULL, &info) != ERR_OK)
            connecting = false;
    }

    if (announce) {
        char payload[100];
        const int length = snprintf(payload, sizeof payload,
            "{\"schema_version\":1,\"link_id\":\"%s\",\"status\":\"online\"}", link_id);
        if (mqtt_publish(client, availability_topic, payload, (u16_t)length, 1, 1, NULL, NULL) == ERR_OK) {
            announce = false;
            online = dirty = true;
        }
    }
    if (online && sub_stage < 2) {
        // QoS 1 subscriptions, retried until the broker accepts them (the ACL may refuse).
        static uint64_t next_subscribe;
        if (now >= next_subscribe) {
            next_subscribe = now + 2000;
            mqtt_subscribe(client, sub_stage ? wifi_topic : command_topic, 1, subscribe_done, NULL);
        }
    }
    pegatron_ack ack;
    while (online && queue_try_peek(settings.acks, &ack)) {
        send_ack(ack.request_id, ack.executed, ack.reason, modes[(unsigned)ack.mode < 4 ? (unsigned)ack.mode : 0]);
        queue_try_remove(settings.acks, &ack);
    }
    if (online && (dirty || now >= next_publish) && publish_snapshot(snapshot, now)) {
        dirty = false;
        next_publish = now + 1000;
    }
    if (online) publish_changes(snapshot);
}

