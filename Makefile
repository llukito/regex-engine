CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -I src
SRC     = src/ast.c src/parser.c src/nfa.c src/dfa.c src/regex.c
OBJ     = $(SRC:src/%.c=build/%.o)

.PHONY: all test clean demo match

all: build/test_parser build/test_nfa build/test_dfa build/test_minimize build/test_regex build/match

build:
	mkdir -p build

build/%.o: src/%.c src/ast.h src/parser.h src/nfa.h src/dfa.h src/regex.h src/charset.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/test_parser: tests/test_parser.c $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ tests/test_parser.c $(OBJ)

build/test_nfa: tests/test_nfa.c $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ tests/test_nfa.c $(OBJ)

build/test_dfa: tests/test_dfa.c $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ tests/test_dfa.c $(OBJ)

build/test_minimize: tests/test_minimize.c $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ tests/test_minimize.c $(OBJ)

build/test_regex: tests/test_regex.c $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ tests/test_regex.c $(OBJ)

build/match: src/match.c $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ src/match.c $(OBJ)

test: build/test_parser build/test_nfa build/test_dfa build/test_minimize build/test_regex
	./build/test_parser
	./build/test_nfa
	./build/test_dfa
	./build/test_minimize
	./build/test_regex

match: build/match
	@echo "Usage: ./build/match [--min] [--icase] <pattern> <string>"

build/demo: tests/demo.c $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ tests/demo.c $(OBJ)

demo: build/demo
	@echo "Usage: ./build/demo 'pattern'"

clean:
	rm -rf build
