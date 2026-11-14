CC := gcc
SDCC ?= sdcc
PYTHON ?= python3
COMMON := src/wire.c src/pd.c src/dp.c
HOST_FLAGS := -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -Itests
SDCC_FLAGS := -mmcs51 --model-small --std-sdcc11 --opt-code-size -Iinclude
LINK_FLAGS := --iram-size 256 --xram-loc 0 --xram-size 256 --code-size 14336
TARGET_REL := build/target/wire.rel build/target/pd.rel build/target/dp.rel build/target/ch543_phy.rel build/target/ch543_board.rel
.DELETE_ON_ERROR:
.PHONY: all test sanitize firmware clean reports
all: test firmware
test: build/test_protocol build/test_phy
	./build/test_protocol
	./build/test_phy
build/test_phy: tests/test_phy.c hal/ch543_phy.c $(wildcard include/*.h)
	@mkdir -p build
	$(CC) $(HOST_FLAGS) -O2 tests/test_phy.c -o $@
build/test_protocol: $(COMMON) hal/mock.c tests/test_protocol.c $(wildcard include/*.h) tests/mock.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) -O2 $(COMMON) hal/mock.c tests/test_protocol.c -o $@
sanitize: build/test_protocol_sanitize build/test_phy_sanitize
	ASAN_OPTIONS=detect_leaks=1 ./build/test_protocol_sanitize
	ASAN_OPTIONS=detect_leaks=1 ./build/test_phy_sanitize
build/test_phy_sanitize: tests/test_phy.c hal/ch543_phy.c $(wildcard include/*.h)
	@mkdir -p build
	$(CC) $(HOST_FLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_phy.c -o $@
build/test_protocol_sanitize: $(COMMON) hal/mock.c tests/test_protocol.c $(wildcard include/*.h) tests/mock.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(COMMON) hal/mock.c tests/test_protocol.c -o $@
build/target/%.rel: src/%.c $(wildcard include/*.h)
	@mkdir -p build/target
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@
build/target/%.rel: hal/%.c $(wildcard include/*.h)
	@mkdir -p build/target
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@
build/target/main-source.rel: src/main.c $(wildcard include/*.h)
	@mkdir -p build/target
	$(SDCC) $(SDCC_FLAGS) -DDEFAULT_DP_ROLE=1 -c $< -o $@
build/target/main-sink.rel: src/main.c $(wildcard include/*.h)
	@mkdir -p build/target
	$(SDCC) $(SDCC_FLAGS) -DDEFAULT_DP_ROLE=2 -c $< -o $@
build/ch543-dp-source.ihx: $(TARGET_REL) build/target/main-source.rel tools/check_vectors.py tools/memory_report.py
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) build/target/main-source.rel $(TARGET_REL) -o $@
	$(PYTHON) tools/check_vectors.py $@
	$(PYTHON) tools/memory_report.py $@
build/ch543-dp-sink.ihx: $(TARGET_REL) build/target/main-sink.rel tools/check_vectors.py tools/memory_report.py
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) build/target/main-sink.rel $(TARGET_REL) -o $@
	$(PYTHON) tools/check_vectors.py $@
	$(PYTHON) tools/memory_report.py $@
build/%.hex: build/%.ihx
	packihx $< > $@
firmware: build/ch543-dp-source.hex build/ch543-dp-sink.hex
reports: firmware
	$(PYTHON) tools/memory_report.py build/ch543-dp-source.ihx
	$(PYTHON) tools/memory_report.py build/ch543-dp-sink.ihx
clean:
	rm -rf build
