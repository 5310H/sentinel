CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -D_GNU_SOURCE

# Paths - Root is one level up from test_service
ROOT        = ..
COMMS_DIR   = $(ROOT)/components/comms
STORAGE_DIR = $(ROOT)/components/storage
HAL_DIR     = $(ROOT)/components/hal

# Include directories
INCLUDES = -I. -I$(COMMS_DIR) -I$(STORAGE_DIR) -I$(HAL_DIR)

# Source Files
SRCS = sentinel_test.c \
       $(COMMS_DIR)/dispatcher.c \
       $(COMMS_DIR)/noonlight.c \
       $(COMMS_DIR)/smtp.c \
       $(STORAGE_DIR)/storage_mgr.c \
       $(STORAGE_DIR)/cJSON.c \
       $(HAL_DIR)/hal_mock_gpio.c

# Object Files mapping
OBJS = $(SRCS:.c=.o)
TARGET = sentinel_test

# --- Build Rules ---

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TARGET) $(OBJS) -lcurl -lm -lssl -lcrypto

# Rule for local files (sentinel_test.c)
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Rule for component files (handles the ../ prefix)
$(ROOT)/%.o: $(ROOT)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(TARGET) *.o
	rm -f $(COMMS_DIR)/*.o
	rm -f $(STORAGE_DIR)/*.o
	rm -f $(HAL_DIR)/*.o

.PHONY: all clean