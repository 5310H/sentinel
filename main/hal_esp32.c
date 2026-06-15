#include "hal.h"
#include "hal_esp32.h"
#include "driver/gpio.h"
#include "storage_mgr.h"

// Externs for configuration arrays defined in storage_mgr.c/main.c
extern zone_t zones[];
extern int z_count;
extern relay_t relays[];
extern int r_count;

// Default pins for Siren/Strobe if not dynamically configured
#ifndef GPIO_SIREN
#define GPIO_SIREN 25
#endif
#ifndef GPIO_STROBE
#define GPIO_STROBE 26
#endif

void hal_esp32_init(void) {
    // 1. Configure Zones (Inputs)
    gpio_config_t io_conf = {0};
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1; // Default to pull-up for NC sensors

    for (int i = 0; i < z_count; i++) {
        if (zones[i].gpio >= 0) {
            io_conf.pin_bit_mask = (1ULL << zones[i].gpio);
            gpio_config(&io_conf);
        }
    }

    // 2. Configure Relays (Outputs)
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;

    for (int i = 0; i < r_count; i++) {
        if (relays[i].gpio >= 0) {
            io_conf.pin_bit_mask = (1ULL << relays[i].gpio);
            gpio_config(&io_conf);
            gpio_set_level(relays[i].gpio, 0); // Default OFF
        }
    }

    // 3. Configure Siren & Strobe
    io_conf.pin_bit_mask = (1ULL << GPIO_SIREN) | (1ULL << GPIO_STROBE);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_SIREN, 0);
    gpio_set_level(GPIO_STROBE, 0);
}

void hal_set_relay(int gpio, bool state) {
    if (gpio >= 0) {
        gpio_set_level(gpio, state ? 1 : 0);
    }
}

bool hal_get_relay_state(int gpio) {
    if (gpio >= 0) {
        return gpio_get_level(gpio);
    }
    return false;
}

int hal_get_zone_state(int gpio) {
    if (gpio >= 0) {
        return gpio_get_level(gpio);
    }
    return 1; // Default high
}

void hal_set_siren(bool state) {
    gpio_set_level(GPIO_SIREN, state ? 1 : 0);
}

void hal_set_strobe(bool state) {
    gpio_set_level(GPIO_STROBE, state ? 1 : 0);
}