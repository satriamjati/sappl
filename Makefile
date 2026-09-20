# # Build with mingw32-make (llvm-mingw-20260908-ucrt-x86_64)
SRC_DIR = src
TARGET = sappl.exe
SRC = $(SRC_DIR)/sappl.cpp
RES = $(SRC_DIR)/resource.res

CXXFLAGS = -std=c++20
LDFLAGS = -mwindows -municode -lgdi32 -ldwmapi

all: $(TARGET)

$(RES): $(SRC_DIR)/resource.rc $(SRC_DIR)/resource.h $(SRC_DIR)/sappl.ico
	windres $< -O coff -o $@

$(TARGET): $(SRC) $(RES)
	$(CXX) $(CXXFLAGS) $(SRC) $(RES) -o $(TARGET) $(LDFLAGS)

clean:
	del /Q $(TARGET) $(SRC_DIR)\*.res