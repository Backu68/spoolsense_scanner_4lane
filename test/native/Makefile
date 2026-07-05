CXX = g++
CC = gcc
CXXFLAGS = -std=c++17 -Wall -Wextra -DNATIVE_TEST -I../../src -I../../include -I. \
           -I../../lib/openprinttag -I../../lib/opentag3d -I../../lib/tigertag -I../../lib/bambutag
CFLAGS = -std=c11 -DNATIVE_TEST -I../../lib/openprinttag

OPT_OBJS = openprinttag_lib.o cbor_native.o

.PHONY: all clean test

all: test_printer_manager

openprinttag_lib.o: ../../lib/openprinttag/openprinttag_lib.c
	$(CC) $(CFLAGS) -c -o $@ $<

cbor_native.o: ../../lib/openprinttag/cbor_native.c
	$(CC) $(CFLAGS) -c -o $@ $<

test_printer_manager: test_printer_manager.cpp native_stubs.cpp MockPrinterStrategy.h test_helpers.h ../../src/IPrinterStrategy.h $(OPT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ test_printer_manager.cpp native_stubs.cpp $(OPT_OBJS)

test: test_printer_manager
	@echo ""
	@echo "=== Running PrinterManager tests ==="
	@./test_printer_manager
	@echo ""

clean:
	rm -f test_printer_manager *.o
