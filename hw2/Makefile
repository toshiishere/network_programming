# === Compiler and Flags ===
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra

# === Build Mode (default = release) ===
MODE ?= release

ifeq ($(MODE),debug)
    CXXFLAGS += -g -O0 -DDEBUG
else ifeq ($(MODE),release)
    CXXFLAGS += -O2
else
    $(error Invalid MODE '$(MODE)'. Use 'debug' or 'release')
endif

# === Source Files ===
COMMON_SRCS := utility.cpp
HEADERS     := utility.h

# === Targets ===
TARGETS := data_server.out game_server.out

# === Default rule ===
all: $(TARGETS)

# --- Individual builds ---
data_server.out: data_server.cpp $(COMMON_SRCS) $(HEADERS) 
	$(CXX) $(CXXFLAGS) data_server.cpp $(COMMON_SRCS) -o $@

game_server.out: game_server.cpp $(COMMON_SRCS) $(HEADERS) tetris.cpp tetris.h
	$(CXX) $(CXXFLAGS) game_server.cpp $(COMMON_SRCS) tetris.cpp -o $@

# --- Clean up ---
clean:
	rm -f *.out *.o

# --- Rebuild everything ---
rebuild: clean all

.PHONY: all clean rebuild
