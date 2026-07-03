# --- COMPILER & EXECUTABLE (Native x86_64, NOT cross-compiler) ---
CC = gcc
CFLAGS_BASE = -g -O0 -Wall
TARGET = sentinel_test

# --- DIRECTORIES ---
COMP_BASE = ../components
STORAGE_DIR  = $(COMP_BASE)/storage
MONGOOSE_DIR = $(COMP_BASE)/mongoose
MQTT_APP_DIR = $(COMP_BASE)/mqtt_app
ENGINE_DIR   = $(COMP_BASE)/engine
HAL_DIR      = $(COMP_BASE)/hardware
SECURITY_DIR = $(COMP_BASE)/security
CAMERA_DIR   = $(COMP_BASE)/camera
NETWORK_WATCHDOG_DIR = $(COMP_BASE)/network_watchdog
# --- ESPHome API Components ---
ESPHOME_API_DIR = ../components/esphome_api
ESPHOME_C_API_DIR = ../components/esphome-c-api
NOISE_DIR = ../components/noise-c

# --- INCLUDES ---
INCLUDES = -I$(STORAGE_DIR) -I$(MONGOOSE_DIR) \
           -I$(HAL_DIR) -I$(MQTT_APP_DIR) -I$(ENGINE_DIR) -I$(SECURITY_DIR) -I$(CAMERA_DIR) -I$(ESPHOME_API_DIR) -I$(NETWORK_WATCHDOG_DIR) \
           -I$(ESPHOME_C_API_DIR)/include -I$(ESPHOME_C_API_DIR)/src/proto -I$(ESPHOME_C_API_DIR)/src -I$(NOISE_DIR)/include -I$(NOISE_DIR)/src/protocol -I.

# --- COMPILER FLAGS (Native Linux build, NOT ESP-IDF) ---
CFLAGS = $(CFLAGS_BASE) $(INCLUDES) \
         -DMG_ENABLE_MQTT=1 \
         -DMG_ENABLE_HTTP=1 \
         -DMG_ENABLE_TLS=1 \
         -DMG_ENABLE_OPENSSL=1 \
         -DMG_TLS=MG_TLS_OPENSSL

# --- LINKER FLAGS (Order matters on Ubuntu!) ---
# --no-as-needed forces the inclusion of SSL symbols we saw in nm
LIBS = -Wl,--no-as-needed -lssl -lcrypto -lcurl -lcjson -lpthread -lm -L$(NOISE_DIR)/src/protocol -lnoiseprotocol

# --- HARDWARE WRAPPERS ---
WRAPS = -Wl,--wrap=hal_get_zone_state \
        -Wl,--wrap=hal_set_relay \
        -Wl,--wrap=hal_set_siren \
        -Wl,--wrap=hal_set_strobe \
        -Wl,--wrap=engine_check_keypad \
        -Wl,--wrap=hal_get_relay_state

# --- OBJECT FILES ---
OBJ = $(STORAGE_DIR)/storage_mgr.o \
      $(ENGINE_DIR)/dispatcher.o \
      $(ENGINE_DIR)/noonlight.o \
      $(ENGINE_DIR)/telegram.o \
      $(ENGINE_DIR)/net.o \
      $(ENGINE_DIR)/ha_mqtt.o \
      $(ENGINE_DIR)/otp.o \
      $(MONGOOSE_DIR)/mongoose.o \
      $(MQTT_APP_DIR)/mqtt_logic.o \
      $(ENGINE_DIR)/smtp.o \
      $(ENGINE_DIR)/user_mgr.o \
      $(ENGINE_DIR)/logging.o \
      $(ENGINE_DIR)/engine.o \
      $(ESPHOME_API_DIR)/esphome_api_client.o \
      $(ESPHOME_API_DIR)/esphome_zones.o \
      $(ESPHOME_C_API_DIR)/src/api/esphome_api.o \
      $(ESPHOME_C_API_DIR)/src/api/frame.o \
      $(ESPHOME_C_API_DIR)/src/api/proto_helpers.o \
      $(ESPHOME_C_API_DIR)/src/noise/noise.o \
      $(ESPHOME_C_API_DIR)/src/proto/api.pb.o \
      $(ESPHOME_C_API_DIR)/src/proto/api_options.pb.o \
      $(ESPHOME_C_API_DIR)/src/proto/pb_common.o \
      $(ESPHOME_C_API_DIR)/src/proto/pb_decode.o \
      $(ESPHOME_C_API_DIR)/src/proto/pb_encode.o \
      $(ESPHOME_C_API_DIR)/src/protobuf/messages.o \
      $(ESPHOME_C_API_DIR)/src/transport/transport.o \
      ../components/tuya/tuya_zones.o \
      $(ENGINE_DIR)/monitor.o \
      $(ENGINE_DIR)/system_monitoring.o \
      $(SECURITY_DIR)/session_mgr.o \
      $(SECURITY_DIR)/audit_log.o \
      $(CAMERA_DIR)/camera_zones.o \
      $(NETWORK_WATCHDOG_DIR)/network_watchdog.o \
      matter_mock.o \
      esphome_mock.o \
      camera_mock.o \
      tuya_mock.o \
      main_mock.o

# --- BUILD RULES ---

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LIBS) $(WRAPS)

test_encrypted_connection: test_encrypted_connection.c $(ESPHOME_API_DIR)/esphome_api_protocol.o $(NANOPB_DIR)/pb_common.o $(NANOPB_DIR)/pb_decode.o $(NANOPB_DIR)/pb_encode.o $(GENERATED_PROTO_DIR)/api.pb.o
	$(CC) $(CFLAGS) test_encrypted_connection.c $(ESPHOME_API_DIR)/esphome_api_protocol.o $(NANOPB_DIR)/pb_common.o $(NANOPB_DIR)/pb_decode.o $(NANOPB_DIR)/pb_encode.o $(GENERATED_PROTO_DIR)/api.pb.o -o test_encrypted_connection $(LIBS)

test_noise: test_noise.c
	$(CC) $(CFLAGS) test_noise.c -L$(NOISE_DIR) -lnoiseprotocol -o test_noise $(LIBS)

test_esphome_client: test_esphome_client.c
	$(CC) $(CFLAGS) test_esphome_client.c -L$(NOISE_DIR) -lnoiseprotocol -o test_esphome_client $(LIBS)

test_esphome_basic: test_esphome_basic.c
	$(CC) $(CFLAGS) test_esphome_basic.c -o test_esphome_basic $(LIBS)

# Standard rule for local .c files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Pattern rule for component .c files
$(COMP_BASE)/%.o: $(COMP_BASE)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ)
	rm -f $(TARGET) test_encrypted_connection test_noise test_esphome_client test_esphome_basic

.PHONY: all clean