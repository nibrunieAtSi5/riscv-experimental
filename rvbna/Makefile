CXX      ?= g++
CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -O2 -std=c99
CXXFLAGS ?= -Wall -Wextra -O2 -std=c++17

TARGET = rvbna_bench

OBJS = rvbna_bench.o rvbna_bench_wrapper.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

rvbna_bench.o: rvbna_bench.c rvbna.h
	$(CC) $(CFLAGS) -c -o $@ $<

rvbna_bench_wrapper.o: rvbna_bench_wrapper.cpp bulknormdot.h softfloat.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
