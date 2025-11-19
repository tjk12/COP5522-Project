# Standard compiler
CXX = g++
# MPI compiler wrapper
MPICXX = mpic++
# Common Compiler flags: C++17 standard, Level 3 optimization, and OpenMP support
CXXFLAGS = -std=c++17 -O3 -march=native
# Common Include directories for third-party libraries (like json.hpp)
INCLUDES = -Iinclude
# Combine common flags
COMMON_FLAGS = $(CXXFLAGS) $(INCLUDES)
# OpenMP specific flags
OMP_FLAGS = -fopenmp
# Define all target executables
TARGETS = \
	basic_seq_sorter \
	optimized_seq_sorter \
	basic_omp_sorter \
	optimized_mpi_sorter

# Common flags for all builds
all: $(TARGETS)

# Rule for the basic sequential sorter
basic_seq_sorter: basic_seq_proj.cpp
	$(CXX) $(COMMON_FLAGS) -o $@ $<

# Rule for the optimized sequential sorter (includes omp simd)
optimized_seq_sorter: optimized_seq_proj.cpp
	$(CXX) $(COMMON_FLAGS) $(OMP_FLAGS) -o $@ $<

# Rule for the original OpenMP-only parallel sorter
basic_omp_sorter: basic_parallel_proj.cpp
	$(CXX) $(COMMON_FLAGS) $(OMP_FLAGS) -o $@ $<

# Rule for the optimized hybrid MPI + OpenMP sorter
optimized_mpi_sorter: optimized_parallel_proj.cpp
	$(MPICXX) $(COMMON_FLAGS) $(OMP_FLAGS) -o $@ $<

# --- Cleanup Rule ---
clean:
	rm -f $(TARGETS)