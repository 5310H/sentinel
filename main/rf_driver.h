#ifndef RF_DRIVER_H
#define RF_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//! @brief Last received RF code
extern uint32_t last_rf_code;

//! @brief Initialize the RF receiver driver
void rf_driver_init(void);

#ifdef __cplusplus
}
#endif

#endif 
