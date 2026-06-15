#ifndef HAL_ESP32_H
#define HAL_ESP32_H

#ifdef __cplusplus
extern "C" {
#endif

// Initialize ESP32 specific hardware (GPIOs, etc.)
void hal_esp32_init(void);

#ifdef __cplusplus
}
#endif

#endif // HAL_ESP32_H