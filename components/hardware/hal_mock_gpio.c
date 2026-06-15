#include <stdio.h>
#include <stdbool.h>

void hal_gpio_write(int pin, int level) {
    printf("[HAL] GPIO PIN %d set to %s\n", pin, level ? "HIGH (ALARM ACTIVE)" : "LOW");
}

bool hal_gpio_read(int pin) {
    // Simulated read: always returns low unless we were to add test injection
    return false; 
}