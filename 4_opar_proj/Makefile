CXX = mpic++
CXXFLAGS = -std=c++17 -O3 -march=native -fopenmp
INCLUDES = -I../include
TARGET = optimized_mpi_sorter
SRC = optimized_parallel_proj.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) $(TARGET).exe

.PHONY: all clean
