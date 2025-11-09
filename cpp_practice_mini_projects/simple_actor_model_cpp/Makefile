# === Makefile ===
CXX = clang++
CXXFLAGS = -std=c++20 -Wall -Wextra -pthread -fno-omit-frame-pointer
SRC = actor_model_test.cpp                   # change this to your actual .cpp sources
TARGET = actor_system

# Default: plain debug build
debug: CXXFLAGS += -O0 -g -DDEBUG -D_GLIBCXX_ASSERTIONS
debug: $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET)_debug $(SRC)

# Sanitizer build (ASan + UBSan)
sanitize: CXXFLAGS += -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer -DDEBUG
sanitize: $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET)_asan $(SRC)

# Thread Sanitizer build
tsan: CXXFLAGS += -O1 -g -fsanitize=thread -fno-omit-frame-pointer -DDEBUG
tsan: $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET)_tsan $(SRC)

# Release build (for performance testing)
release: CXXFLAGS += -O3 -DNDEBUG
release: $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET)_release $(SRC)

# Cleanup
clean:
	rm -f $(TARGET)_debug $(TARGET)_asan $(TARGET)_tsan $(TARGET)_release ./log/*
