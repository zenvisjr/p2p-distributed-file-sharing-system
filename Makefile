# Makefile

CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17
LDFLAGS_CLIENT = -lssl -lcrypto

SRC_DIR = .
TEST_DIR = test
BUILD_DIR = build

TARGETS = \
	$(BUILD_DIR)/load_balancer \
	$(BUILD_DIR)/tracker \
	$(BUILD_DIR)/client \
	$(BUILD_DIR)/test

all: $(BUILD_DIR) $(TARGETS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/load_balancer: $(SRC_DIR)/load_balancer.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/tracker: $(SRC_DIR)/tracker.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/client: $(SRC_DIR)/client.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS_CLIENT)

$(BUILD_DIR)/test: $(TEST_DIR)/test.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
