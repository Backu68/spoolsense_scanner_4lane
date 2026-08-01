CXX = g++
CC = gcc
CXXFLAGS = -std=c++17 -Wall -Wextra -DNATIVE_TEST -I../../src -I../../include -I. \
           -I../../lib/openprinttag -I../../lib/opentag3d -I../../lib/tigertag -I../../lib/bambutag
CFLAGS = -std=c11 -DNATIVE_TEST -I../../lib/openprinttag

OPT_OBJS = openprinttag_lib.o cbor_native.o

.PHONY: all clean test

all: test_printer_manager test_opt_ndef_bounds test_diagnostics test_tft_landscape_layout test_tigertag_parser test_bambutag_parser

openprinttag_lib.o: ../../lib/openprinttag/openprinttag_lib.c
	$(CC) $(CFLAGS) -c -o $@ $<

cbor_native.o: ../../lib/openprinttag/cbor_native.c
	$(CC) $(CFLAGS) -c -o $@ $<

test_printer_manager: test_printer_manager.cpp native_stubs.cpp MockPrinterStrategy.h test_helpers.h ../../src/IPrinterStrategy.h $(OPT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ test_printer_manager.cpp native_stubs.cpp $(OPT_OBJS)

test_opt_ndef_bounds: test_opt_ndef_bounds.c $(OPT_OBJS)
	$(CC) $(CFLAGS) -o $@ test_opt_ndef_bounds.c $(OPT_OBJS)

test_diagnostics: test_diagnostics.cpp ../../src/DiagnosticsUtil.cpp ../../src/DiagnosticsUtil.h ../../src/DiagnosticsTypes.h
	$(CXX) $(CXXFLAGS) -o $@ test_diagnostics.cpp ../../src/DiagnosticsUtil.cpp

test_tft_landscape_layout: test_tft_landscape_layout.cpp ../../src/TFTLandscapeLayout.cpp ../../src/TFTLandscapeLayout.h
	$(CXX) $(CXXFLAGS) -o $@ test_tft_landscape_layout.cpp ../../src/TFTLandscapeLayout.cpp

test_tigertag_parser: test_tigertag_parser.cpp ../../lib/tigertag/TigerTagParser.cpp ../../lib/tigertag/TigerTagParser.h
	$(CXX) $(CXXFLAGS) -o $@ test_tigertag_parser.cpp ../../lib/tigertag/TigerTagParser.cpp

test_bambutag_parser: test_bambutag_parser.cpp ../../lib/bambutag/BambuTagParser.cpp ../../lib/bambutag/BambuTagParser.h
	$(CXX) $(CXXFLAGS) -o $@ test_bambutag_parser.cpp ../../lib/bambutag/BambuTagParser.cpp


test: test_printer_manager test_opt_ndef_bounds test_diagnostics test_tft_landscape_layout test_tigertag_parser test_bambutag_parser
	@echo ""
	@echo "=== Running PrinterManager tests ==="
	@./test_printer_manager
	@echo ""
	@echo "=== Running openprinttag NDEF bounds tests ==="
	@./test_opt_ndef_bounds
	@echo ""
	@echo "=== Running Diagnostics tests ==="
	@./test_diagnostics
	@echo ""
	@echo "=== Running Landscape layout tests ==="
	@./test_tft_landscape_layout
	@echo ""
	@echo "=== Running TigerTag parser tests ==="
	@./test_tigertag_parser
	@echo ""
	@echo "=== Running Bambu parser tests ==="
	@./test_bambutag_parser
	@echo ""

clean:
	rm -f test_printer_manager test_opt_ndef_bounds test_diagnostics \
	      test_tft_landscape_layout test_tigertag_parser test_bambutag_parser *.o
