CXX = g++
CXXFLAGS = -std=c++17 -O3 -march=native
INCLUDES = -I../include
TARGET = basic_seq_sorter
SRC = basic_seq_proj.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) $(TARGET).exe

.PHONY: all clean
