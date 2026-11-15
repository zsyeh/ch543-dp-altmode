CC := gcc
SDCC ?= sdcc
PYTHON ?= python3
COMMON := src/wire.c src/pd.c src/dp.c
HOST_FLAGS := -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -Itests
SDCC_FLAGS := -mmcs51 --model-small --std-sdcc11 --opt-code-size -Iinclude
LINK_FLAGS := --iram-size 256 --xram-loc 0 --xram-size 256 --code-size 14336
BOARD2_VBUS_VERIFIED ?= 0
ifeq ($(filter $(BOARD2_VBUS_VERIFIED),0 1),)
$(error BOARD2_VBUS_VERIFIED must be 0 or 1)
endif
BOARD2_FLAGS := -DBOARD2=1 -DBOARD2_VBUS_VERIFIED=$(BOARD2_VBUS_VERIFIED)
BOARD2_LABEL := $(if $(filter 1,$(BOARD2_VBUS_VERIFIED)),vbus-verified,guarded)
BOARD2_BASE := build/board2/ch543-board2-dp
REFERENCE_BASE := build/reference/ch543-reference-dp
BOARD2_REL := $(addprefix build/board2/target/,wire.rel pd.rel dp.rel ch543_phy.rel ch543_board2.rel ch9445.rel soft_i2c.rel)
TARGET_REL := $(addprefix build/reference/target/,wire.rel pd.rel dp.rel ch543_phy.rel ch543_board.rel)
DIAG_REL := $(addprefix build/diagnostic/target/,main.rel board2_diag.rel soft_i2c.rel)
.DELETE_ON_ERROR:
.PHONY: all test sanitize firmware reference diagnostic clean reports
all: test firmware
EXTRA_TESTS := test_ch9445 test_i2c test_board2_hal test_board2_protocol test_board2_diag
test: build/test_protocol build/test_phy $(addprefix build/,$(EXTRA_TESTS))
	./build/test_protocol
	./build/test_phy
	./build/test_ch9445
	./build/test_i2c
	./build/test_board2_hal
	./build/test_board2_protocol
	./build/test_board2_diag
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
	ASAN_OPTIONS=detect_leaks=1 ./build/test_board2_diag_sanitize
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
$(eval $(call HOST_TEST_RULES,test_board2_diag,,-DBOARD2))
build/test_board2_diag build/test_board2_diag_sanitize: src/board2_diag.c
build/reference/target/%.rel: src/%.c $(wildcard include/*.h)
	@mkdir -p build/reference/target
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@
build/reference/target/%.rel: hal/%.c $(wildcard include/*.h)
	@mkdir -p build/reference/target
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@
build/reference/target/main-source.rel: src/main.c $(wildcard include/*.h)
	@mkdir -p build/reference/target
	$(SDCC) $(SDCC_FLAGS) -DDEFAULT_DP_ROLE=1 -c $< -o $@
build/reference/target/main-sink.rel: src/main.c $(wildcard include/*.h)
	@mkdir -p build/reference/target
	$(SDCC) $(SDCC_FLAGS) -DDEFAULT_DP_ROLE=2 -c $< -o $@
$(REFERENCE_BASE)-source.ihx: $(TARGET_REL) build/reference/target/main-source.rel tools/check_vectors.py tools/memory_report.py tools/estimate_stack.py
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) build/reference/target/main-source.rel $(TARGET_REL) -o $@
	$(PYTHON) tools/check_vectors.py $@
	$(PYTHON) tools/memory_report.py $@
	$(PYTHON) tools/estimate_stack.py $(dir $@)target --image $@
$(REFERENCE_BASE)-sink.ihx: $(TARGET_REL) build/reference/target/main-sink.rel tools/check_vectors.py tools/memory_report.py tools/estimate_stack.py
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) build/reference/target/main-sink.rel $(TARGET_REL) -o $@
	$(PYTHON) tools/check_vectors.py $@
	$(PYTHON) tools/memory_report.py $@
	$(PYTHON) tools/estimate_stack.py $(dir $@)target --image $@
build/%.hex: build/%.ihx
	packihx $< > $@
# Default is the user's CH9445 board. Reference GPIO firmware is opt-in.
firmware: board2 diagnostic
reference: $(REFERENCE_BASE)-source.hex $(REFERENCE_BASE)-sink.hex
.PHONY: board2
board2: $(BOARD2_BASE)-source-$(BOARD2_LABEL).hex $(BOARD2_BASE)-sink-$(BOARD2_LABEL).hex
	@$(PYTHON) tools/image_manifest.py $(filter %.hex,$^) --vbus-verified $(BOARD2_VBUS_VERIFIED)
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
$(BOARD2_BASE)-source-$(BOARD2_LABEL).ihx: $(BOARD2_REL) build/board2/target/main-source.rel tools/check_vectors.py tools/memory_report.py tools/estimate_stack.py
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) build/board2/target/main-source.rel $(BOARD2_REL) -o $@
	$(PYTHON) tools/check_vectors.py $@ --board2
	$(PYTHON) tools/memory_report.py $@
	$(PYTHON) tools/estimate_stack.py $(dir $@)target --image $@
$(BOARD2_BASE)-sink-$(BOARD2_LABEL).ihx: $(BOARD2_REL) build/board2/target/main-sink.rel tools/check_vectors.py tools/memory_report.py tools/estimate_stack.py
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) build/board2/target/main-sink.rel $(BOARD2_REL) -o $@
	$(PYTHON) tools/check_vectors.py $@ --board2
	$(PYTHON) tools/memory_report.py $@
	$(PYTHON) tools/estimate_stack.py $(dir $@)target --image $@
build/diagnostic/target/%.rel: src/%.c $(wildcard include/*.h)
	@mkdir -p build/diagnostic/target
	$(SDCC) $(SDCC_FLAGS) -DBOARD2 -DBOARD2_DIAGNOSTIC -c $< -o $@
build/diagnostic/target/%.rel: hal/%.c $(wildcard include/*.h)
	@mkdir -p build/diagnostic/target
	$(SDCC) $(SDCC_FLAGS) -DBOARD2 -DBOARD2_DIAGNOSTIC -c $< -o $@
build/diagnostic/ch543-board2-diagnostic-uart115200.ihx: $(DIAG_REL) tools/check_vectors.py tools/memory_report.py tools/estimate_stack.py
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) $(DIAG_REL) -o $@
	$(PYTHON) tools/check_vectors.py $@ --diagnostic
	$(PYTHON) tools/memory_report.py $@
	$(PYTHON) tools/estimate_stack.py $(dir $@)target --image $@ --no-interrupts
diagnostic: build/diagnostic/ch543-board2-diagnostic-uart115200.hex
reports: firmware reference
	$(PYTHON) tools/memory_report.py $(REFERENCE_BASE)-source.ihx
	$(PYTHON) tools/memory_report.py $(REFERENCE_BASE)-sink.ihx
	$(PYTHON) tools/memory_report.py $(BOARD2_BASE)-source-$(BOARD2_LABEL).ihx
	$(PYTHON) tools/memory_report.py $(BOARD2_BASE)-sink-$(BOARD2_LABEL).ihx
	$(PYTHON) tools/memory_report.py build/diagnostic/ch543-board2-diagnostic-uart115200.ihx
clean:
	rm -rf build
