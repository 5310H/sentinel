#ifndef HAL_H
#define HAL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Hardware Abstraction Layer
int hal_get_zone_state(int gpio);
void hal_set_relay(int gpio, bool state);
bool hal_get_relay_state(int gpio);
void hal_set_siren(bool state);
void hal_set_strobe(bool state);

#ifdef __cplusplus
}
#endif

#endif // HAL_H