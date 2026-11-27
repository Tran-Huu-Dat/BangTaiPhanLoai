#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

// Bench test: GP10/GP11 share a PWM slice. Start at HOME, move each servo
// separately by a small amount, then return HOME. Remove pusher arms first.
static constexpr uint SERVO1_PIN = 10;
static constexpr uint SERVO2_PIN = 11;
static constexpr uint16_t HOME_US = 1500;
static constexpr uint16_t TEST_US = 1700;
static_assert(TEST_US > HOME_US && TEST_US <= 2000, "Test pulse out of range");

static void move(uint pin, uint16_t pulse_us) {
    pwm_set_gpio_level(pin, pulse_us);
    printf("GP%u = %u us\n", pin, pulse_us);
}

static void cycle() {
    move(SERVO1_PIN, TEST_US);
    sleep_ms(500);
    move(SERVO1_PIN, HOME_US);
    sleep_ms(700);
    move(SERVO2_PIN, TEST_US);
    sleep_ms(500);
    move(SERVO2_PIN, HOME_US);
    sleep_ms(700);
}

int main() {
    stdio_init_all();
    gpio_set_function(SERVO1_PIN, GPIO_FUNC_PWM);
    gpio_set_function(SERVO2_PIN, GPIO_FUNC_PWM);
    const uint slice = pwm_gpio_to_slice_num(SERVO1_PIN);
    if (slice != pwm_gpio_to_slice_num(SERVO2_PIN)) return 1;
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, static_cast<float>(clock_get_hz(clk_sys)) / 1000000.0f);
    pwm_config_set_wrap(&config, 19999);  // 1 MHz counter, 50 Hz PWM.
    pwm_init(slice, &config, false);
    move(SERVO1_PIN, HOME_US);
    move(SERVO2_PIN, HOME_US);
    pwm_set_enabled(slice, true);

    sleep_ms(5000);  // Time to open the serial monitor and watch both servos.
    printf("TWO SERVO TEST: 3 cycles, then HOME. Send 'b' to repeat.\n");
    for (int i = 0; i < 3; ++i) cycle();
    printf("DONE: both channels at HOME\n");
    while (true) {
        if (getchar_timeout_us(0) == 'b') {
            cycle();
            printf("DONE: both channels at HOME\n");
        }
        sleep_ms(10);
    }
}
