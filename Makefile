CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -I src
SRC     = src/ast.c src/parser.c
OBJ     = $(SRC:src/%.c=build/%.o)

.PHONY: all test clean demo

all: build/test_parser

build:
	mkdir -p build

build/%.o: src/%.c src/ast.h src/parser.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/test_parser: tests/test_parser.c $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ tests/test_parser.c $(OBJ)

test: build/test_parser
	./build/test_parser

# Tiny interactive demo: print the AST for a pattern given on the command line.
build/demo: tests/demo.c $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ tests/demo.c $(OBJ)

demo: build/demo
	@echo "Usage: ./build/demo 'pattern'"

clean:
	rm -rf build
