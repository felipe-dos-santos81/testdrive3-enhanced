# Makefile for Test Drive III Enhanced — macOS (Apple Silicon) build and run.
# It wraps the CMake build in CMakeLists.txt; every compile flag lives there. The game is windowed and the
# mouse cursor is never captured, so no target here passes --fullscreen or anything that would grab input.
#   make help                      the targets below
#   make run CAR=CCNSX SCALE=4     play (see the variables at the top)
#   make check                     load Game/TDIII.EXE, print a summary, exit
#   make smoke                     headless: compose frames, save snapshots, exit
SERVICE = Test Drive III Enhanced

# Build
BUILD_DIR  = build
BUILD_TYPE ?= Release
GENERATOR  ?= Ninja
ARCH       ?= arm64
GAME       = $(BUILD_DIR)/testdrive3-enhanced

# Run
GAME_DIR ?= Game
SCALE    ?= 3

# Headless smoke test
SMOKE_DIR     ?= $(BUILD_DIR)/smoke
SMOKE_SECONDS ?= 6

# Passed to the game only when set: CAR COURSE SKILL SOUND FRAME_TICKS RES AA KEYS, and ARGS for the rest.
#   make run CAR=CCNSX COURSE=SCENE02 SKILL=3        make run ARGS="--classic --haze 40"

.PHONY: help install configure build run check smoke clean

# ── Environment ───────────────────────────────────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32m${SERVICE} — macOS (Apple Silicon)\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [var=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-26s\033[0m %s\n", $$1, $$2}'

install: ## Install cmake, ninja and sdl3 with Homebrew (only what is missing), then configure
	@missing=""; \
	for tool in cmake ninja; do \
		command -v $$tool >/dev/null || missing="$$missing $$tool"; \
	done; \
	brew list sdl3 >/dev/null 2>&1 || missing="$$missing sdl3"; \
	if [ -n "$$missing" ]; then \
		echo "Installing with Homebrew:$$missing"; \
		brew install $$missing; \
	else \
		echo "cmake, ninja and sdl3 are already installed."; \
	fi
	@$(MAKE) --no-print-directory configure
	@echo "Environment setup complete."

configure: ## Configure the CMake build (Release, Ninja, arm64, no launcher)
	@cmake -S . -B $(BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_OSX_ARCHITECTURES=$(ARCH) \
		-DTD3E_LAUNCHER=OFF
	@echo "Configuration complete."

# ── Build ──────────────────────────────────────────────────────────────────────────────────────────────

build: configure ## Build testdrive3-enhanced
	@cmake --build $(BUILD_DIR)

# ── Run ─────────────────────────────────────────────────────────────────────────────────────────────────

run: build ## Play the game, windowed (usage: make run [CAR=CODE] [COURSE=CODE] [SKILL=N] [SCALE=N] [ARGS="..."])
	@$(GAME) --game-dir "$(GAME_DIR)" --scale "$(SCALE)" \
		$(if $(CAR),--car "$(CAR)") \
		$(if $(COURSE),--course "$(COURSE)") \
		$(if $(SKILL),--skill "$(SKILL)") \
		$(if $(SOUND),--sound "$(SOUND)") \
		$(if $(FRAME_TICKS),--frame-ticks "$(FRAME_TICKS)") \
		$(if $(RES),--res-scale "$(RES)") \
		$(if $(AA),--aa "$(AA)") \
		$(if $(KEYS),--keys "$(KEYS)") \
		$(ARGS)

check: build ## Check that Game/TDIII.EXE loads, then exit
	@$(GAME) --game-dir "$(GAME_DIR)" --check

# ── Smoke test ────────────────────────────────────────────────────────────────────────────────────────

# Proves the engine boots, reads the game files and composes frames without a human watching: the dummy video
# driver needs no window, and TD3_SNAPSHOT_DIR (src/host.h) saves a BMP of every frame presented at least two
# seconds after the previous one. It shows the boot screen, not a race; `make run` is the proof for that.
smoke: build ## Run headless for a few seconds and save snapshots, then check that frames were composed
	@rm -rf "$(SMOKE_DIR)"; mkdir -p "$(SMOKE_DIR)"
	@SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy \
		TD3_SNAPSHOT_DIR="$(SMOKE_DIR)" $(GAME) --game-dir "$(GAME_DIR)" & \
	pid=$$!; sleep $(SMOKE_SECONDS); kill $$pid 2>/dev/null; wait $$pid 2>/dev/null; \
	n=$$(ls -1 "$(SMOKE_DIR)" | wc -l | tr -d ' '); \
	if [ "$$n" -lt 1 ]; then \
		echo "smoke: no snapshots in $(SMOKE_DIR) — nothing was composed"; exit 1; \
	fi; \
	echo "smoke: $$n snapshot(s) in $(SMOKE_DIR)"; ls -l "$(SMOKE_DIR)"

# ── Development ───────────────────────────────────────────────────────────────────────────────────────

clean: ## Remove the build directory
	rm -rf $(BUILD_DIR)
	@echo "Cleanup complete."
