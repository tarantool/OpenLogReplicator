# Variables:
#   ORACLE_TARGET     — Oracle environment: xe-21 | free-23 (default: free-23)
#   TESTGEN_TIMEOUT   — per-scenario timeout in seconds (default: 300)
#
# Examples:
#   make test ORACLE_TARGET=xe-21          # Full flow: up + testgen + down
#
# See `make help` for how the image is built.

TESTS_DIR := $(CURDIR)/tests
SCRIPTS_DIR := $(TESTS_DIR)/scripts

# Oracle target (xe-21 or free-23)
ORACLE_TARGET ?= free-23
ENV_DIR := $(TESTS_DIR)/1-environments/$(ORACLE_TARGET)

OLR_BIN ?= /opt/OpenLogReplicator/OpenLogReplicator

# Colors for output
BLUE := \033[36m
GREEN := \033[32m
YELLOW := \033[33m
RED := \033[31m
RESET := \033[0m

.PHONY: help up down testgen test
.DEFAULT_GOAL := help

# Helper: print colored message
define log
	@echo "$(BLUE)[OLR]$(RESET) $(1)"
endef

# Helper: print success message
define success
	@echo "$(GREEN)[✓]$(RESET) $(1)"
endef

# Validate ORACLE_TARGET
define check-oracle-target
	@if [ "$(ORACLE_TARGET)" != "xe-21" ] && [ "$(ORACLE_TARGET)" != "free-23" ]; then \
		echo "$(RED)[✗]$(RESET) Invalid ORACLE_TARGET='$(ORACLE_TARGET)'. Use: xe-21 or free-23"; \
		exit 1; \
	fi
endef

## help: Show this help message
help:
	@echo ""
	@echo "$(BLUE)OpenLogReplicator Test$(RESET)"
	@echo "======================"
	@echo ""
	@echo "$(GREEN)Commands (run inside the olr-test image):$(RESET)"
	@echo "  make test     ORACLE_TARGET=xe-21   Full flow: up + testgen + down"
	@echo "  make up       ORACLE_TARGET=xe-21   Start oracle + apply archivelog/grants"
	@echo "  make testgen  ORACLE_TARGET=xe-21   Generate + validate fixtures for every scenario"
	@echo "  make down     ORACLE_TARGET=xe-21   Stop oracle + remove volumes"
	@echo ""
	@echo "$(YELLOW)Building the Docker image:$(RESET)"
	@echo "  The olr-test image is built by the openlogreplicator-docker-vk repo"
	@echo "  (Dockerfile, with --build-arg WITHTESTS=1). For local CI runs:"
	@echo "    cd ../openlogreplicator-docker-vk && \\"
	@echo "    docker build -f Dockerfile --build-arg WITHTESTS=1 \\"
	@echo "                 --build-arg BASE_IMAGE=<vk-base-tag> \\"
	@echo "                 -t olr-test:local ."
	@echo ""

# ----------------------------------------------------------------------------
# Test targets — run INSIDE the olr-test image (built by docker-vk).
# The flow:
#   1. spins up the oracle container via `docker compose up -d --wait`,
#   2. runs `generate.sh` to produce + validate fixtures (sqlplus + cp via
#      `docker exec`); each scenario's OLR output is checked against LogMiner.
# No olr compose-service, no DOCKER_HOST tricks, no shared volumes.
# ----------------------------------------------------------------------------
COMPOSE := docker compose -f $(ENV_DIR)/docker-compose.yaml

## up: Bring up the oracle container + apply OLR setup
up:
	$(call check-oracle-target)
	$(call log,"Starting Oracle $(ORACLE_TARGET)...")
	@$(COMPOSE) up -d --wait
	$(call log,"Applying OLR setup: archivelog + grants...")
	@$(TESTS_DIR)/scripts/oracle-setup.sh $(ORACLE_TARGET)
	$(call success,"Oracle $(ORACLE_TARGET) up + configured")

## down: Stop the oracle container + remove volumes
down:
	$(call check-oracle-target)
	@-$(COMPOSE) down -v --remove-orphans
	$(call success,"Oracle $(ORACLE_TARGET) down")

## testgen: Run generate.sh for every scenario (live-streamed, timeout-guarded)
TESTGEN_TIMEOUT ?= 300
TIMEOUT_BIN := $(shell command -v timeout 2>/dev/null || command -v gtimeout 2>/dev/null)
testgen:
	$(call check-oracle-target)
	$(call log,"Generating fixtures for $(ORACLE_TARGET)...")
	@cd $(CURDIR) && set -uo pipefail; \
		FAILED=""; TOTAL=0; \
		for sql in tests/0-inputs/*.sql; do \
			name=$$(basename "$$sql" .sql); \
			TOTAL=$$((TOTAL + 1)); \
			echo "================ $$name ================"; \
			if $(if $(TIMEOUT_BIN),$(TIMEOUT_BIN) --foreground $(TESTGEN_TIMEOUT),) \
				env ORACLE_TARGET=$(ORACLE_TARGET) tests/scripts/generate.sh "$$name"; then \
				echo "PASS: $$name"; \
			else \
				rc=$$?; \
				if [ "$$rc" = "124" ]; then \
					echo "TIMEOUT after $(TESTGEN_TIMEOUT)s: $$name"; \
				else \
					echo "ERROR: $$name (rc=$$rc)"; \
				fi; \
				FAILED="$$FAILED $$name"; \
			fi; \
		done; \
		if [ -n "$$FAILED" ]; then \
			echo "FAILED:$$FAILED"; exit 1; \
		fi; \
		echo "All $$TOTAL passed"

## test: Full flow — up, testgen, down (cleanup on exit)
test:
	$(call check-oracle-target)
	@set -e; \
		trap '$(MAKE) down ORACLE_TARGET=$(ORACLE_TARGET)' EXIT; \
		$(MAKE) up      ORACLE_TARGET=$(ORACLE_TARGET); \
		$(MAKE) testgen ORACLE_TARGET=$(ORACLE_TARGET)
