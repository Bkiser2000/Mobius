# MobiusPL Makefile
.PHONY: all build clean test check-deps version help install

CC := gcc
CFLAGS := -O2 -Wall -Wextra -std=c11 -DPOSIX_C_SOURCE=200809L
LDFLAGS := -lcrypto -lm

# Build flags with version info
VERSION_FLAGS := -DMOBIUS_VERSION=\"1.0.0\" -DMOBIUS_BUILD_DATE=\"$(shell date -u '+%Y-%m-%d')\" -DMOBIUS_COMMIT=\"$(shell git rev-parse --short HEAD 2>/dev/null || echo 'unknown')\"

TRAINER := mobius_pl
SCORER := mobius_score

all: check-deps build

check-deps:
	@pkg-config --exists openssl || (echo "ERROR: libssl-dev not found"; exit 1)
	@which gcc >/dev/null || (echo "ERROR: gcc not found"; exit 1)
	@echo "Dependencies OK"

build: $(TRAINER) $(SCORER)

$(TRAINER): mobius_pl.c feature_schema_artifact.c
	$(CC) $(CFLAGS) $(VERSION_FLAGS) mobius_pl.c feature_schema_artifact.c $(LDFLAGS) -o $@
	@echo "Built: $@"

$(SCORER): mobius_score.c feature_schema_artifact.c
	$(CC) $(CFLAGS) $(VERSION_FLAGS) mobius_score.c feature_schema_artifact.c $(LDFLAGS) -o $@
	@echo "Built: $@"

version:
	@echo "MobiusPL v1.0.0"

test: build test-unit test-integration test-audit
	@echo "All tests passed!"

test-unit: build
	@if [ -f test/unit.sh ]; then chmod +x test/unit.sh && test/unit.sh ./$(TRAINER) ./$(SCORER); else echo "test/unit.sh not found"; fi

test-integration: build
	@if [ -f test/integration.sh ]; then chmod +x test/integration.sh && test/integration.sh ./$(TRAINER) ./$(SCORER); else echo "test/integration.sh not found"; fi

test-audit: build
	@if [ -f test/audit_verify.sh ]; then chmod +x test/audit_verify.sh && test/audit_verify.sh ./$(SCORER); else echo "test/audit_verify.sh not found"; fi

install: build
	sudo install -m 755 $(TRAINER) $(SCORER) /usr/local/bin/

clean:
	rm -f $(TRAINER) $(SCORER) *.o decision_audit.jsonl model_v1.bin feature_schema_v1.art

help:
	@echo "MobiusPL Makefile"
	@echo "make build   - Compile binaries"
	@echo "make clean   - Remove artifacts"
	@echo "make install - Install to /usr/local/bin"
	@echo "make version - Show version"