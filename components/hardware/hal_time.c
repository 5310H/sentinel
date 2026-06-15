#include "hal_time.h"
#include <stdio.h>
#include <time.h>

#ifdef ESP_PLATFORM
    /* ESP32 Specific Includes */
    #include "esp_sntp.h"
    #include "esp_log.h"
#else
    /* Linux Mock Specific Includes */
    #include <unistd.h>
#endif

void hal_time_init(void) {
#ifdef ESP_PLATFORM
    /* ESP32 hardware initialization */
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    /* Set Timezone for Solon, Ohio (Eastern Time) */
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();
    printf("ESP32 RTC Sync Started...\n");
#else
    /* Mock needs no init, Linux handles its own clock sync */
    printf("Linux Mock Time Ready.\n");
#endif
}

void get_sentinel_time(char *buffer, size_t len) {
    time_t now;
    struct tm timeinfo;
    
    time(&now); // Routes to either PC clock or ESP32 RTC

#ifdef ESP_PLATFORM
    localtime_r(&now, &timeinfo);
#else
    struct tm *info = localtime(&now);
    timeinfo = *info;
#endif

    // Format for Keypad/Logs: "12/27 21:05:30"
    strftime(buffer, len, "%m/%d %H:%M:%S", &timeinfo);
}