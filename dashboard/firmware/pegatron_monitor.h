#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pico/util/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// The monitor never touches GPIO. It validates MQTT commands and hands them to
// the control core through a queue; the control core executes and answers.
typedef enum { PEGATRON_IDLE, PEGATRON_SORTING, PEGATRON_MAINTENANCE, PEGATRON_FAULT } pegatron_mode;
typedef enum { PEGATRON_ENABLE, PEGATRON_PAUSE, PEGATRON_TEST, PEGATRON_CALIBRATE } pegatron_action;
typedef struct {
    pegatron_action action;
    uint8_t station;           // 1 or 2 for TEST/CALIBRATE, else 0.
    char request_id[65];
} pegatron_command;
typedef struct {
    char request_id[65];
    bool executed;
    pegatron_mode mode;        // Mode read back after executing.
    char reason[24];           // "OK" when executed.
} pegatron_ack;
typedef enum { PEGATRON_UNKNOWN, PEGATRON_FALSE, PEGATRON_TRUE } pegatron_flag;

typedef struct {
    bool height_known;
    int16_t height_mm;
    bool distance_known;
    uint16_t distance_mm;
    bool sample_valid;
    pegatron_flag home;
    uint32_t push_count;
    bool reference_known;
    int16_t reference_mm;
} pegatron_station;

typedef struct {
    pegatron_mode mode;
    pegatron_flag estop;
    bool home_timeout[2];
    bool tof_unavailable[2];
    pegatron_station station[2];
    // NULL means allowed. Otherwise a static reason string.
    // Order: ENABLE, PAUSE, TEST 1/2, CALIBRATE 1/2.
    const char *deny[6];
    bool tray_confirmed[3];    // S, M, L. True only for a commissioned sensor.
    uint32_t tray_count[3];
} pegatron_snapshot;

typedef struct {
    const char *device_id;     // 1–48 ASCII letters, numbers, underscores or hyphens.
    const char *ssid;
    const char *wifi_password;
    const char *broker_ipv4;
    const char *mqtt_user;
    const char *mqtt_password;
    uint16_t mqtt_port;
    queue_t *commands;         // pegatron_command, monitor -> control core.
    queue_t *acks;             // pegatron_ack, control core -> monitor.
} pegatron_settings;

// Settings strings must outlive the monitor. Call once on the network core.
bool pegatron_monitor_init(const pegatron_settings *settings);

// Call on the same network core, never from an ISR. Pass a coherent snapshot
// copied from the existing control loop. Set changed for mode/config changes.
void pegatron_monitor_tick(const pegatron_snapshot *snapshot, bool changed);

#ifdef __cplusplus
}
#endif

