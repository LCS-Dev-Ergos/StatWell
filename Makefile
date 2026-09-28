# ================================================================================= #
# ----------------------------- Makefile for StatWell ----------------------------- #
# ================================================================================= #
#
# Thin convenience layer over CMake and CTest. All local build outputs go under
# builds/<profile>; override CXX_COMPILER or GTEST_SOURCE_DIR for local toolchains.
#
# ================================================================================= #

PROFILE ?= debug
GTEST_SOURCE_DIR ?=
# Prefer the managed toolchains on Nix-equipped hosts; remain portable elsewhere.
NIX_USER_BIN := /etc/profiles/per-user/$(shell id -un)/bin
CLANG_COMPILER ?= $(if $(wildcard $(NIX_USER_BIN)/clang++),$(NIX_USER_BIN)/clang++,clang++)
GCC_COMPILER ?= $(if $(wildcard $(NIX_USER_BIN)/g++),$(NIX_USER_BIN)/g++,g++)

BUILD_DIR := builds/$(PROFILE)
CXX_COMPILER := $(CLANG_COMPILER)
BUILD_TYPE := Debug
EXTRA_CXX_FLAGS :=
EXTRA_LINK_FLAGS :=
PLATFORM_SOURCE := src/platform/linux.cpp

ifeq ($(shell uname -s),Darwin)
  PLATFORM_SOURCE := src/platform/darwin.cpp
endif

ifeq ($(PROFILE),release)
  BUILD_TYPE := Release
else ifeq ($(PROFILE),asan)
  EXTRA_CXX_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
  EXTRA_LINK_FLAGS := -fsanitize=address,undefined
else ifeq ($(PROFILE),tsan)
  EXTRA_CXX_FLAGS := -fsanitize=thread -fno-omit-frame-pointer
  EXTRA_LINK_FLAGS := -fsanitize=thread
else ifeq ($(PROFILE),gcc)
  CXX_COMPILER := $(GCC_COMPILER)
  ifeq ($(shell uname -s),Darwin)
    EXTRA_CXX_FLAGS := -F/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks -Wno-elaborated-enum-base
  endif
else ifneq ($(PROFILE),debug)
  $(error Unknown PROFILE '$(PROFILE)'; choose debug, release, asan, tsan, or gcc)
endif

.DEFAULT_GOAL := build
.PHONY: build configure compdb test run format format-check tidy nix-build help clean

configure:
	cmake -S . -B $(BUILD_DIR) -G Ninja \
		-DCMAKE_CXX_COMPILER="$(CXX_COMPILER)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_CXX_FLAGS="$(EXTRA_CXX_FLAGS)" \
		-DCMAKE_EXE_LINKER_FLAGS="$(EXTRA_LINK_FLAGS)" \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DSTATWELL_BUILD_TESTS=ON \
		-DSTATWELL_GTEST_SOURCE_DIR="$(GTEST_SOURCE_DIR)"

compdb: configure
	ln -sfn $(BUILD_DIR)/compile_commands.json compile_commands.json

build: compdb
	cmake --build $(BUILD_DIR) --parallel

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

run: build
	$(BUILD_DIR)/statwell sample $(ARGS)

format:
	clang-format -i include/statwell/*.hpp src/*.cpp src/*.hpp src/cli/*.cpp src/platform/*.cpp src/platform/*.hpp tests/*.cpp

format-check:
	clang-format --dry-run --Werror include/statwell/*.hpp src/*.cpp src/*.hpp src/cli/*.cpp src/platform/*.cpp src/platform/*.hpp tests/*.cpp

tidy: build
	clang-tidy -p $(BUILD_DIR) src/metrics.cpp src/packages.cpp src/registry.cpp src/runtime.cpp src/watch.cpp \
		src/platform/linux_parsers.cpp $(PLATFORM_SOURCE) src/cli/main.cpp \
		tests/metrics_test.cpp tests/linux_parsers_test.cpp

nix-build:
	nix build .#statwell --no-link

clean:
	rm -f compile_commands.json
	rm -rf builds

help:
	@echo 'make [build|compdb|test|run|format|format-check|tidy|nix-build|clean]'
	@echo 'PROFILE=debug|release|asan|tsan|gcc (default: debug)'
	@echo 'Override CLANG_COMPILER, GCC_COMPILER, GTEST_SOURCE_DIR, or ARGS as needed.'
