CC := gcc
SDCC ?= sdcc
PYTHON ?= python3
COMMON := src/wire.c src/pd.c src/dp.c
HOST_FLAGS := -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -Itests
SDCC_FLAGS := -mmcs51 --model-small --std-sdcc11 --opt-code-size -Iinclude
LINK_FLAGS := --iram-size 256 --xram-loc 0 --xram-size 256 --code-size 14336
BOARD2_VBUS_VERIFIED ?= 0
BOARD2_FLAGS := -DBOARD2=1 -DBOARD2_VBUS_VERIFIED=$(BOARD2_VBUS_VERIFIED)
BOARD2_REL := $(addprefix build/board2/target/,wire.rel pd.rel dp.rel ch543_phy.rel ch543_board2.rel ch9445.rel soft_i2c.rel)
TARGET_REL := build/target/wire.rel build/target/pd.rel build/target/dp.rel build/target/ch543_phy.rel build/target/ch543_board.rel
.DELETE_ON_ERROR:
.PHONY: all test sanitize firmware clean reports
all: test firmware
EXTRA_TESTS := test_ch9445 test_i2c test_board2_hal test_board2_protocol
test: build/test_protocol build/test_phy $(addprefix build/,$(EXTRA_TESTS))
	./build/test_protocol
	./build/test_phy
	./build/test_ch9445
	./build/test_i2c
	./build/test_board2_hal
	./build/test_board2_protocol
	$(PYTHON) tools/check_board2_routes.py
build/test_phy: tests/test_phy.c hal/ch543_phy.c $(wildcard include/*.h)
	@mkdir -p build
	$(CC) $(HOST_FLAGS) -O2 tests/test_phy.c -o $@
build/test_protocol: $(COMMON) hal/mock.c tests/test_protocol.c $(wildcard include/*.h) tests/mock.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) -O2 $(COMMON) hal/mock.c tests/test_protocol.c -o $@
sanitize: build/test_protocol_sanitize build/test_phy_sanitize $(addprefix build/,$(addsuffix _sanitize,$(EXTRA_TESTS)))
	ASAN_OPTIONS=detect_leaks=1 ./build/test_protocol_sanitize
	ASAN_OPTIONS=detect_leaks=1 ./build/test_phy_sanitize
	ASAN_OPTIONS=detect_leaks=1 ./build/test_ch9445_sanitize
	ASAN_OPTIONS=detect_leaks=1 ./build/test_i2c_sanitize
	ASAN_OPTIONS=detect_leaks=1 ./build/test_board2_hal_sanitize
	ASAN_OPTIONS=detect_leaks=1 ./build/test_board2_protocol_sanitize
build/test_phy_sanitize: tests/test_phy.c hal/ch543_phy.c $(wildcard include/*.h)
	@mkdir -p build
	$(CC) $(HOST_FLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_phy.c -o $@
build/test_protocol_sanitize: $(COMMON) hal/mock.c tests/test_protocol.c $(wildcard include/*.h) tests/mock.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(COMMON) hal/mock.c tests/test_protocol.c -o $@
define HOST_TEST_RULES
build/$(1): tests/$(1).c $(2) $(wildcard include/*.h) tests/mock.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(3) -O2 tests/$(1).c $(2) -o $$@
build/$(1)_sanitize: tests/$(1).c $(2) $(wildcard include/*.h) tests/mock.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(3) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/$(1).c $(2) -o $$@
endef
$(eval $(call HOST_TEST_RULES,test_ch9445,hal/ch9445.c,))
$(eval $(call HOST_TEST_RULES,test_i2c,hal/soft_i2c.c,-DI2C_TEST))
$(eval $(call HOST_TEST_RULES,test_board2_hal,,-DBOARD2))
build/test_board2_hal build/test_board2_hal_sanitize: hal/ch543_board2.c
$(eval $(call HOST_TEST_RULES,test_board2_protocol,$(COMMON) hal/mock.c hal/ch9445.c,-DBOARD2 -DMOCK_CH9445))
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
build/ch543-dp-source.ihx: $(TARGET_REL) build/target/main-source.rel tools/check_vectors.py tools/memory_report.py tools/estimate_stack.py
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) build/target/main-source.rel $(TARGET_REL) -o $@
	$(PYTHON) tools/check_vectors.py $@
	$(PYTHON) tools/memory_report.py $@
	$(PYTHON) tools/estimate_stack.py $(dir $@)target --image $@
build/ch543-dp-sink.ihx: $(TARGET_REL) build/target/main-sink.rel tools/check_vectors.py tools/memory_report.py tools/estimate_stack.py
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) build/target/main-sink.rel $(TARGET_REL) -o $@
	$(PYTHON) tools/check_vectors.py $@
	$(PYTHON) tools/memory_report.py $@
	$(PYTHON) tools/estimate_stack.py $(dir $@)target --image $@
build/%.hex: build/%.ihx
	packihx $< > $@
firmware: build/ch543-dp-source.hex build/ch543-dp-sink.hex
firmware: board2
.PHONY: board2
board2: build/board2/ch543-dp-source.hex build/board2/ch543-dp-sink.hex
.PHONY: FORCE
FORCE:
build/board2/config.flags: FORCE
	@mkdir -p build/board2
	@$(PYTHON) -c 'from pathlib import Path; p=Path("$@"); s="$(BOARD2_FLAGS)\n"; p.write_text(s) if not p.exists() or p.read_text()!=s else None'
build/board2/target/%.rel: src/%.c $(wildcard include/*.h) build/board2/config.flags
	@mkdir -p build/board2/target
	$(SDCC) $(SDCC_FLAGS) $(BOARD2_FLAGS) -c $< -o $@
build/board2/target/%.rel: hal/%.c $(wildcard include/*.h) build/board2/config.flags
	@mkdir -p build/board2/target
	$(SDCC) $(SDCC_FLAGS) $(BOARD2_FLAGS) -c $< -o $@
build/board2/target/main-source.rel: src/main.c $(wildcard include/*.h) build/board2/config.flags
	@mkdir -p build/board2/target
	$(SDCC) $(SDCC_FLAGS) $(BOARD2_FLAGS) -DDEFAULT_DP_ROLE=1 -c $< -o $@
build/board2/target/main-sink.rel: src/main.c $(wildcard include/*.h) build/board2/config.flags
	@mkdir -p build/board2/target
	$(SDCC) $(SDCC_FLAGS) $(BOARD2_FLAGS) -DDEFAULT_DP_ROLE=2 -c $< -o $@
build/board2/ch543-dp-source.ihx: $(BOARD2_REL) build/board2/target/main-source.rel tools/check_vectors.py tools/memory_report.py tools/estimate_stack.py
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) build/board2/target/main-source.rel $(BOARD2_REL) -o $@
	$(PYTHON) tools/check_vectors.py $@ --board2
	$(PYTHON) tools/memory_report.py $@
	$(PYTHON) tools/estimate_stack.py $(dir $@)target --image $@
build/board2/ch543-dp-sink.ihx: $(BOARD2_REL) build/board2/target/main-sink.rel tools/check_vectors.py tools/memory_report.py tools/estimate_stack.py
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) build/board2/target/main-sink.rel $(BOARD2_REL) -o $@
	$(PYTHON) tools/check_vectors.py $@ --board2
	$(PYTHON) tools/memory_report.py $@
	$(PYTHON) tools/estimate_stack.py $(dir $@)target --image $@
reports: firmware
	$(PYTHON) tools/memory_report.py build/ch543-dp-source.ihx
	$(PYTHON) tools/memory_report.py build/ch543-dp-sink.ihx
	$(PYTHON) tools/memory_report.py build/board2/ch543-dp-source.ihx
	$(PYTHON) tools/memory_report.py build/board2/ch543-dp-sink.ihx
clean:
	rm -rf build
