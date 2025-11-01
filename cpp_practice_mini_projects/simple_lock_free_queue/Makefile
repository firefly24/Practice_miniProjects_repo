CC = clang++
#SRC_FILES = lock_free_queue.cpp
#OBJ_NAME = lock_free_queue.exe
TEST_DIR = test
BUILD_DIR = $(TEST_DIR)/build
MOVE_ONLY_TEST = spsc_test_unique_ptr
COPY_TYPE_TEST = lock_free_queue
SRC_TESTS = $(MOVE_ONLY_TEST) $(COPY_TYPE_TEST)
SRC_FILES = $(TEST_DIR)/src/$(SRC_TESTS).cpp
COMPILER_FLAGS = -std=c++20 -Wall -O3 -g
NUM_ELEMENTS = 10000000
QUEUE_SIZE = 1024


all: dir_present move_only copyable

dir_present:
	mkdir -p $(TEST_DIR)
	mkdir -p $(BUILD_DIR)

move_only: $(TEST_DIR)/src/$(MOVE_ONLY_TEST).cpp
	$(CC) $(COMPILER_FLAGS) $(TEST_DIR)/src/$(MOVE_ONLY_TEST).cpp -o $(BUILD_DIR)/$(MOVE_ONLY_TEST).exe

copyable: $(TEST_DIR)/src/$(COPY_TYPE_TEST).cpp
	$(CC) $(COMPILER_FLAGS) $(TEST_DIR)/src/$(COPY_TYPE_TEST).cpp -o $(BUILD_DIR)/$(COPY_TYPE_TEST).exe

clean:
	rm -r $(BUILD_DIR)/*