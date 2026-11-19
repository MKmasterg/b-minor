.DEFAULT_GOAL := all

CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic

.PHONY: all clean test test-core test-lexer
all: build/bminor-highlight

build/bminor-highlight: build/src/main.o build/src/source.o build/src/lexer.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/test-core: build/tests/core.o build/src/source.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/test-lexer: build/tests/lexer.o build/src/source.o build/src/lexer.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: %.cpp
	mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

test: test-core test-lexer

test-core: build/test-core
	./build/test-core

test-lexer: build/test-lexer
	./build/test-lexer

clean:
	rm -rf build

-include $(wildcard build/src/*.d build/tests/*.d)
