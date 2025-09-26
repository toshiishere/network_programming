CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++17

# sources and objects
SRCS := server.cpp playerV2.cpp game.cpp
OBJS := $(SRCS:.cpp=.o)

# output directory
OUTDIR := out
TARGET_SERVER := $(OUTDIR)/server.exe
TARGET_PLAYER := $(OUTDIR)/playerV2.exe


all: $(TARGET_SERVER) $(TARGET_PLAYER)

debug: CXXFLAGS += -g
debug: all

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(TARGET_SERVER): server.o game.o | $(OUTDIR)
	$(CXX) $(CXXFLAGS) -o $@ server.o

$(TARGET_PLAYER): playerV2.o game.o | $(OUTDIR)
	$(CXX) $(CXXFLAGS) -o $@ playerV2.o game.o

%.o: %.cpp game.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o
	rm -rf $(OUTDIR)

.PHONY: all clean debug

