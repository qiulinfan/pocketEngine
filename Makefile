# Top-level convenience wrapper for CMake presets.
# Do not copy generated build/*/Makefile into the repo.

CMAKE ?= cmake
MKDOCS ?= mkdocs
RSYNC ?= rsync
JOBS ?= 4

DEBUG_PRESET ?= unix-makefiles-debug
RELEASE_PRESET ?= unix-makefiles-release
SITE_BUILD_DIR := .site

DEBUG_BUILD_DIR := build/$(DEBUG_PRESET)
RUNTIME_BIN := $(DEBUG_BUILD_DIR)/game_engine_linux
EDITOR_BIN := $(DEBUG_BUILD_DIR)/game_editor_linux
ROOT_RUNTIME_BIN := game_engine_linux
ROOT_EDITOR_BIN := game_editor_linux

.DEFAULT_GOAL := engine

.PHONY: help configure build configure-release build-release
.PHONY: stage-runtime stage-editor
.PHONY: engine editor run run-editor compile-commands
.PHONY: docs-build docs-serve site-build docs
.PHONY: clean clean-all

configure:
	$(CMAKE) --preset $(DEBUG_PRESET)

build: configure
	$(CMAKE) --build --preset $(DEBUG_PRESET) -j$(JOBS)

configure-release:
	$(CMAKE) --preset $(RELEASE_PRESET)

build-release: configure-release
	$(CMAKE) --build --preset $(RELEASE_PRESET) -j$(JOBS)

stage-runtime: build
	cp $(RUNTIME_BIN) ./$(ROOT_RUNTIME_BIN)

stage-editor: build
	cp $(EDITOR_BIN) ./$(ROOT_EDITOR_BIN)

engine: stage-runtime

editor: stage-editor

run: engine
	./$(ROOT_RUNTIME_BIN)

run-editor: editor
	./$(ROOT_EDITOR_BIN)

compile-commands: configure
	cp $(DEBUG_BUILD_DIR)/compile_commands.json ./compile_commands.json

docs-build:
	$(MKDOCS) build --clean

docs-serve:
	$(MKDOCS) serve

site-build:
	rm -rf $(SITE_BUILD_DIR)
	mkdir -p $(SITE_BUILD_DIR)
	$(RSYNC) -av --delete --exclude 'lua-api/' --exclude '*.md' docs/ $(SITE_BUILD_DIR)/
	$(MKDOCS) build --clean

docs: site-build
	./scripts/deploy_site_to_gh_pages.sh

clean:
	-$(CMAKE) --build --preset $(DEBUG_PRESET) --target clean
	-$(CMAKE) --build --preset $(RELEASE_PRESET) --target clean
	-rm -f ./$(ROOT_RUNTIME_BIN) ./$(ROOT_EDITOR_BIN)

clean-all:
	rm -rf build/$(DEBUG_PRESET) build/$(RELEASE_PRESET)
	rm -f ./$(ROOT_RUNTIME_BIN) ./$(ROOT_EDITOR_BIN)
