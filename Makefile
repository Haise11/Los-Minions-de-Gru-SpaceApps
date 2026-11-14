CC      ?= gcc
CFLAGS  := -Wall -Wextra -std=c11 -O2 -Isrc -Ithird_party
LDFLAGS := -lm

TARGET      := space_health_server
TEST_TARGET := test_engine

CORE_SRCS := \
	src/astronaut.c \
	src/mission.c \
	src/health_engine.c \
	src/storage.c \
	third_party/cJSON.c \
	third_party/mongoose.c

SERVER_SRCS := src/main.c src/server.c $(CORE_SRCS)
TEST_SRCS   := tests/test_engine.c src/server.c $(CORE_SRCS)

.PHONY: all run test clean

all: $(TARGET)

$(TARGET): $(SERVER_SRCS) src/health_types.h src/astronaut.h src/mission.h src/health_engine.h src/storage.h src/server.h third_party/cJSON.h third_party/mongoose.h
	$(CC) $(CFLAGS) $(SERVER_SRCS) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

$(TEST_TARGET): $(TEST_SRCS)
	$(CC) $(CFLAGS) $(TEST_SRCS) -o $(TEST_TARGET) $(LDFLAGS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET) *.o src/*.o third_party/*.o tests/*.o
