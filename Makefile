# Multi-Pool Dynamic Memory Allocator Makefile

CC = gcc
CFLAGS = -g -Wall -Werror -Wno-deprecated-declarations -std=c99 -fPIC -D_DEFAULT_SOURCE
LDFLAGS = -shared -fPIC

# Source files
SOURCES = mm.c bulk.c
OBJECTS = $(SOURCES:.c=.o)
LIBRARY = libcsemalloc.so

# Test files
TEST_SIMPLE = test_simple_malloc
TEST_BULK = test_bulk

.PHONY: all clean tests

all: $(LIBRARY)

# Build the shared library
$(LIBRARY): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

# Compile object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build test executables
$(TEST_SIMPLE): test_simple_malloc.c $(LIBRARY)
	$(CC) $(CFLAGS) -o $@ $< -L. -lcsemalloc

$(TEST_BULK): test_bulk.c $(LIBRARY)
	$(CC) $(CFLAGS) -o $@ $< -L. -lcsemalloc

# Build all tests
tests: $(TEST_SIMPLE) $(TEST_BULK)

# Run tests with the library
test: $(LIBRARY)
	@echo "Testing with Unix commands..."
	@LD_PRELOAD=./$(LIBRARY) ls > /dev/null && echo "✓ ls works"
	@LD_PRELOAD=./$(LIBRARY) echo "test" > /dev/null && echo "✓ echo works"
	@LD_PRELOAD=./$(LIBRARY) pwd > /dev/null && echo "✓ pwd works"

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(LIBRARY) $(TEST_SIMPLE) $(TEST_BULK)
	rm -f *.o *.so test_simple_malloc test_bulk

# Install (optional - copies to /usr/local/lib)
install: $(LIBRARY)
	install -m 755 $(LIBRARY) /usr/local/lib/

# Help
help:
	@echo "Multi-Pool Dynamic Memory Allocator"
	@echo ""
	@echo "Available targets:"
	@echo "  make           - Build the shared library"
	@echo "  make tests     - Build test executables"
	@echo "  make test      - Run basic tests with Unix commands"
	@echo "  make clean     - Remove all build artifacts"
	@echo "  make install   - Install library to /usr/local/lib (requires sudo)"
	@echo ""
	@echo "Usage:"
	@echo "  LD_PRELOAD=./libcsemalloc.so <command>"
