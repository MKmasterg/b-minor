.DEFAULT_GOAL := all

CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic

ANALYSIS_OBJECTS := build/src/source.o build/src/lexer.o build/src/parser.o build/src/highlight.o

.PHONY: all clean test test-core test-lexer test-parser test-recovery test-highlight
all: build/bminor-highlight

build/bminor-highlight: build/src/main.o $(ANALYSIS_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/test-core: build/tests/core.o build/src/source.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/test-lexer: build/tests/lexer.o build/src/source.o build/src/lexer.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/test-parser: build/tests/parser.o build/src/source.o build/src/lexer.o build/src/parser.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/test-recovery: build/tests/recovery.o build/src/source.o build/src/lexer.o build/src/parser.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/test-highlight: build/tests/highlight.o $(ANALYSIS_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: %.cpp
	mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

test: test-core test-lexer test-parser test-recovery test-highlight

test-core: build/test-core
	./build/test-core

test-lexer: build/test-lexer
	timeout 10s ./build/test-lexer

test-parser: build/test-parser
	./build/test-parser

test-recovery: build/test-recovery
	timeout 10s ./build/test-recovery

test-highlight: build/test-highlight
	timeout 10s ./build/test-highlight

clean:
	rm -rf build

-include $(wildcard build/src/*.d build/tests/*.d)
