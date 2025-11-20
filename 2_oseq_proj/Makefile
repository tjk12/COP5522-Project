CXX = g++
CXXFLAGS = -std=c++17 -O3 -march=native -fopenmp
INCLUDES = -I../include
TARGET = optimized_seq_sorter
SRC = optimized_seq_proj.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) $(TARGET).exe

.PHONY: all clean
