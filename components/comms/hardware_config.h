#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

/* I2C Bus Settings */
#define I2C_SDA_PIN      8
#define I2C_SCL_PIN      18

/* I2C Device Addresses */
#define ADDR_PCF8575     0x22  // IO Expander (Relays & DI)
#define ADDR_EEPROM      0x50  // 24C02 Storage
#define ADDR_RTC         0x68  // DS3231 Timekeeping
#define ADDR_OLED        0x3C  // SSD1306 Display

/* PCF8575 Bit Mapping */
// Low Byte (P0-P7): Relays
#define RELAY_MASK       0x00FF 
// High Byte (P8-P15): Digital Inputs
#define DI_MASK          0xFF00 

/* Analog Inputs (DC 0-5V) */
#define ANALOG_A1_PIN    7
#define ANALOG_A2_PIN    6

/* 1-Wire Sensors */
#define ONE_WIRE_S1      40
#define ONE_WIRE_S2      13

/* Ethernet W5500 (SPI) */
#define W5500_MOSI       2
#define W5500_MISO       41
#define W5500_SCLK       1
#define W5500_CS         42
#define W5500_INT        43
#define W5500_RST        44

#endif