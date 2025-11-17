# Compiler
# Define compilers
CXX = g++
# Compiler flags: C++17 standard, Level 3 optimization, and OpenMP support
CXXFLAGS = -std=c++17 -O3 -fopenmp -march=native
# Include directories for third-party libraries (like json.hpp)
INCLUDES = -Iinclude
# The final executable name
TARGET = sorter
# The source file
SOURCES = main.cpp
MPICXX = mpic++

# --- Rules ---
# Default rule: build the target executable
all: $(TARGET)
# Define compiler flags
# -Iinclude tells the compiler to look for headers in the 'include' directory
# -O3 is a high optimization level
# -fopenmp enables OpenMP for parallelization
# -march=native optimizes for the specific CPU architecture of the build machine
COMMON_FLAGS = -std=c++17 -O3 -march=native -Iinclude
OMP_FLAGS = -fopenmp

# Rule to link the executable
$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(SOURCES)
# Define all target executables
TARGETS = \
	basic_seq_sorter \
	optimized_seq_sorter \
	omp_parallel_sorter \
	basic_mpi_sorter \
	optimized_mpi_sorter

# Rule to clean up generated files
# --- Build Rules ---

# The 'all' rule is the default, it builds all targets
all: $(TARGETS)

# Rule for the original OpenMP-only parallel sorter (renamed for clarity)
omp_parallel_sorter: main.cpp
	$(CXX) $(COMMON_FLAGS) $(OMP_FLAGS) -o $@ $<

# Rule for the basic sequential sorter
basic_seq_sorter: basic_seq_proj.cpp
	$(CXX) $(COMMON_FLAGS) -o $@ $<

# Rule for the optimized sequential sorter (includes omp simd)
optimized_seq_sorter: optimized_seq_proj.cpp
	$(CXX) $(COMMON_FLAGS) $(OMP_FLAGS) -o $@ $<

# Rule for the basic MPI sorter
basic_mpi_sorter: basic_parallel_prog.cpp
	$(MPICXX) $(COMMON_FLAGS) -o $@ $<

# Rule for the optimized hybrid MPI+OpenMP sorter
optimized_mpi_sorter: optimized_parallel_proj.cpp
	$(MPICXX) $(COMMON_FLAGS) $(OMP_FLAGS) -o $@ $<

# --- Cleanup Rule ---

clean:
	rm -f $(TARGET)
	rm -f $(TARGETS)
	rm -f results.json
	rm -rf report