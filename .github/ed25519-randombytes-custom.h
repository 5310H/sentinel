#ifndef ED25519_RANDOMBYTES_CUSTOM_H
#define ED25519_RANDOMBYTES_CUSTOM_H

#include <stddef.h>
#include "esp_random.h"

static void ED25519_FN(ed25519_randombytes_unsafe) (void *p, size_t len) {
    esp_fill_random(p, len);
}

#endif // ED25519_RANDOMBYTES_CUSTOM_H
