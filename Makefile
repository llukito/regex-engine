CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -I src
SRC     = src/ast.c src/parser.c src/nfa.c
OBJ     = $(SRC:src/%.c=build/%.o)

.PHONY: all test clean demo

all: build/test_parser build/test_nfa

build:
	mkdir -p build

build/%.o: src/%.c src/ast.h src/parser.h src/nfa.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/test_parser: tests/test_parser.c $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ tests/test_parser.c $(OBJ)

build/test_nfa: tests/test_nfa.c $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ tests/test_nfa.c $(OBJ)

test: build/test_parser build/test_nfa
	./build/test_parser
	./build/test_nfa

# Tiny interactive demo: print the AST (and NFA) for a pattern.
build/demo: tests/demo.c $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ tests/demo.c $(OBJ)

demo: build/demo
	@echo "Usage: ./build/demo 'pattern'"

clean:
	rm -rf build
