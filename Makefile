# Compiler
CXX = g++-15

# Compiler flags: C++17 standard, Level 3 optimization, and OpenMP support
CXXFLAGS = -std=c++17 -O3 -fopenmp -march=native

# Include directories for third-party libraries (like json.hpp)
INCLUDES = -Iinclude

# The final executable name
TARGET = sorter

# The source file
SOURCES = main.cpp

# --- Rules ---

# Default rule: build the target executable
all: $(TARGET)

# Rule to link the executable
$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(SOURCES)

# Rule to clean up generated files
clean:
	rm -f $(TARGET)