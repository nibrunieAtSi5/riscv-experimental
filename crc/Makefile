CC ?= gcc
RUNNER ?=
CFLAGS ?= -O2 -Wall -I.
LDFLAGS ?=
BUILD_DIR ?= build

TARGET = $(BUILD_DIR)/crc_test
SRCS = crc_test.c crc_model.c crc_rv.c
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(TARGET)
	$(RUNNER) ./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)/*.o $(TARGET)
