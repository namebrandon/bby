# Top-level convenience targets for the bby chess engine.

CMAKE ?= cmake
NINJA ?= ninja
BUILD_DIR ?= build
GENERATOR ?= Ninja

define run_cmake
	$(CMAKE) -S . -B $(BUILD_DIR)/$1 -G $(GENERATOR) -DCMAKE_BUILD_TYPE=$2 $(3)
endef

.PHONY: all configure release debug profile sanitize tsan sanitize-test tsan-test perft clean

all: release

configure:
	$(call run_cmake,RelWithDebInfo,RelWithDebInfo)

release:
	$(call run_cmake,release,Release)
	$(CMAKE) --build $(BUILD_DIR)/release --target bby

debug:
	$(call run_cmake,debug,Debug)
	$(CMAKE) --build $(BUILD_DIR)/debug --target bby

profile:
	$(call run_cmake,profile,RelWithDebInfo)
	$(CMAKE) --build $(BUILD_DIR)/profile --target bby

sanitize:
	$(call run_cmake,sanitize,RelWithDebInfo,-DBBY_SANITIZER=asan-ubsan)
	$(CMAKE) --build $(BUILD_DIR)/sanitize --target bby

tsan:
	$(call run_cmake,tsan,RelWithDebInfo,-DBBY_SANITIZER=tsan)
	$(CMAKE) --build $(BUILD_DIR)/tsan --target bby

sanitize-test: sanitize
	$(CMAKE) --build $(BUILD_DIR)/sanitize --target bby-unit
	ctest --test-dir $(BUILD_DIR)/sanitize --output-on-failure

tsan-test: tsan
	$(CMAKE) --build $(BUILD_DIR)/tsan --target bby-unit
	ctest --test-dir $(BUILD_DIR)/tsan --output-on-failure

perft: release
	$(CMAKE) --build $(BUILD_DIR)/release --target bby-perft
	$(BUILD_DIR)/release/bby-perft --suite test/data/perft_depth6.txt

.PHONY: perft-diff
perft-diff: release
	tools/perft_compare.py external/engines/stockfish-ubuntu-x86-64-bmi2 test/data/perft_depth6.txt --depth 5 --bby $(BUILD_DIR)/release/bby-perft

.PHONY: perft-telemetry
perft-telemetry: release
	tools/perft_telemetry.py --bby $(BUILD_DIR)/release/bby-perft --output out/perft_telemetry.log

clean:
	rm -rf $(BUILD_DIR) out
