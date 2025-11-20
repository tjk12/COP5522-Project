CXX = g++
CXXFLAGS = -std=c++17 -O3 -march=native -fopenmp
INCLUDES = -I../include
TARGET = basic_omp_sorter
SRC = basic_parallel_proj.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) $(TARGET).exe

.PHONY: all clean
