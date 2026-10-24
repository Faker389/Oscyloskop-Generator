RAYLIB_DIR = third_party/raylib
RAYLIB_SRC = $(RAYLIB_DIR)/src

CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -I$(RAYLIB_SRC)
LDFLAGS  = -L$(RAYLIB_SRC) -lraylib -lgdi32 -lwinmm -lopengl32

TARGET = metrologia.exe
SRC    = main.cpp

.PHONY: all run clean raylib

all: $(TARGET)

$(TARGET): $(SRC) engine.cpp $(RAYLIB_SRC)/libraylib.a
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) engine.cpp $(LDFLAGS)

raylib:
	$(MAKE) -C $(RAYLIB_SRC) PLATFORM=PLATFORM_DESKTOP CC=gcc RAYLIB_LIBTYPE=STATIC

run: $(TARGET)
	./$(TARGET)

clean:
	del /Q $(TARGET) 2>nul || rm -f $(TARGET)
