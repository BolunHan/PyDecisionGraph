# C test harness for the decision_graph.decision_tree.bake module.
#
#   make check           build and run every suite with $(CC) (default gcc)
#   make asan            rebuild with ASan + UBSan + LSan and run
#   make clang           rebuild with clang and run
#   make lenient         rebuild with DCG_VIGILANT=0 (empty values, no abort)
#   make dispatch        rebuild with DCG_EVAL_DIRECT_HOOKS=0 (rules found by dispatch)
#   make mutable         rebuild with DCG_MAPPING_IMMUTABLE_DTYPE=0 (a store may retype)
#   make bench           build and run the eval benchmark, both ways
#   make logs            print the collected run logs
#   make format          apply the repo .clang-format to the module and the suites
#   make clean           drop the build directories (keeps the logs)
#   make clean-artifacts drop the collected logs
#
# Every run writes to artifacts/<config>/: one log per suite (stdout and
# stderr together, so sanitizer reports land there too) plus summary.log
# holding the verdict table. That makes a run readable after the fact
# without needing the terminal that produced it.
#
# The module is header-only, so a suite is one .c file plus two include
# roots: this repo (for <decision_graph/...>) and PyCyBase (for <cbase/...>).
# The cbase root is passed with -isystem so third-party warnings cannot fail
# -Werror on our code.
#
# Note: clang-format 22 rejects the repo's .clang-format (it uses the key
# AcrossAccessSpecifiers, which this version no longer accepts), so 'make
# format' reports that error until the key is removed from the config.

# Make predefines CC=cc, which would shadow a plain "CC ?= gcc": only supply
# the default when nobody chose a compiler (command line or environment).
ifeq ($(origin CC),default)
CC        := gcc
endif

ROOT      ?= $(abspath $(CURDIR)/../..)
CBASE     ?= $(HOME)/Projects/PyCyBase/cbase/includes

CFLAGS    ?= -std=gnu11 -Wall -Wextra -Werror -g -O1
INCLUDES   = -I$(ROOT) -isystem $(CBASE)
SANITIZE  ?=
# The eval protocol reaches the platform's own pow() and floor().
LDLIBS    ?= -lm

SUITES     = test_c_var \
             test_c_edge \
             test_c_const \
             test_c_expr \
             test_c_collection \
             test_c_logic_group \
             test_c_node_lifecycle \
             test_c_node_hierarchy \
             test_c_node_callbacks \
             test_c_node_render \
             test_c_eval \
             test_c_bake

BINDIR    ?= build
ARTIFACTS ?= $(CURDIR)/artifacts
CONFIG    ?= $(notdir $(CC))
LOGDIR     = $(ARTIFACTS)/$(CONFIG)
BINS       = $(addprefix $(BINDIR)/, $(SUITES))

# The module is header-only: every suite depends on every header.
HEADERS    = $(wildcard $(ROOT)/decision_graph/decision_tree/bake/*.h) test_util.h

.PHONY: all check asan clang lenient dispatch mutable bench logs format clean clean-artifacts

all: $(BINS)

$(BINDIR)/%: %.c $(HEADERS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(SANITIZE) $(INCLUDES) $< -o $@ $(LDLIBS)

# Build, run and log every suite. The table goes to the terminal and to
# summary.log, so a failure is legible both live and afterwards.
check: $(BINS)
	@mkdir -p $(LOGDIR)
	@summary=$(LOGDIR)/summary.log; \
	{ echo "# bake C test run"; \
	  echo "# config   : $(CONFIG)"; \
	  echo "# compiler : $$($(CC) --version | head -1)"; \
	  echo "# flags    : $(CFLAGS) $(SANITIZE)"; \
	  echo "# date     : $$(date -Is)"; \
	  echo; } > $$summary; \
	status=0; \
	for suite in $(SUITES); do \
	    log=$(LOGDIR)/$$suite.log; \
	    if ./$(BINDIR)/$$suite > $$log 2>&1; then verdict=PASS; else verdict=FAIL; status=1; fi; \
	    printf '%-26s %-5s %s\n' "$$suite" "$$verdict" "$$(tail -1 $$log)" | tee -a $$summary; \
	done; \
	if [ $$status -eq 0 ]; then printf '\nALL SUITES PASSED\n' | tee -a $$summary; \
	else printf '\nSUITE FAILURES - read the per-suite logs\n' | tee -a $$summary; fi; \
	printf 'logs: $(LOGDIR)/\n'; \
	exit $$status

asan:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory \
	    BINDIR=build-asan CONFIG=asan \
	    SANITIZE="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all" \
	    check

clang:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory CC=clang BINDIR=build-clang CONFIG=clang check

# The other side of the DCG_VIGILANT switch: mismatched reads hand back their
# empty value instead of aborting. It also proves the reader code compiles with
# the abort compiled out.
lenient:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory \
	    BINDIR=build-lenient CONFIG=lenient \
	    CFLAGS="-std=gnu11 -Wall -Wextra -Werror -g -O1 -DDCG_VIGILANT=0" \
	    check

# The other side of the eval switch: no node installs its own rule, so every one
# is found by dispatching on its type. The same suites, so the dispatching build
# has to reach every answer the installing one does.
dispatch:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory \
	    BINDIR=build-dispatch CONFIG=dispatch \
	    CFLAGS="-std=gnu11 -Wall -Wextra -Werror -g -O1 -DDCG_EVAL_DIRECT_HOOKS=0" \
	    check

# The other side of the mapping switch: a store is a pool a caller may retype,
# and its reads resolve again when the entry they hold has changed type.
mutable:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory \
	    BINDIR=build-mutable CONFIG=mutable \
	    CFLAGS="-std=gnu11 -Wall -Wextra -Werror -g -O1 -DDCG_MAPPING_IMMUTABLE_DTYPE=0" \
	    check

# The same two builds, timed. Reads the eval cost out of the suites' own graphs:
#   make bench
bench:
	@mkdir -p $(BINDIR)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DDCG_EVAL_DIRECT_HOOKS=0 $(INCLUDES) bench_c_eval.c -o $(BINDIR)/bench_dispatch $(LDLIBS)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DDCG_EVAL_DIRECT_HOOKS=1 $(INCLUDES) bench_c_eval.c -o $(BINDIR)/bench_direct $(LDLIBS)
	@printf '\n--- dispatch (DCG_EVAL_DIRECT_HOOKS=0) ---\n'
	@./$(BINDIR)/bench_dispatch
	@printf '\n--- direct hooks (DCG_EVAL_DIRECT_HOOKS=1) ---\n'
	@./$(BINDIR)/bench_direct

logs:
	@found=0; \
	for file in $(ARTIFACTS)/*/summary.log; do \
	    [ -f "$$file" ] || continue; \
	    found=1; \
	    printf '\n===== %s =====\n' "$$file"; \
	    cat "$$file"; \
	done; \
	[ $$found -eq 1 ] || echo "no logs yet - run: make check | asan | clang"

format:
	clang-format -i $(HEADERS) $(addsuffix .c, $(SUITES))

clean:
	@rm -rf build build-asan build-clang build-lenient build-dispatch build-mutable

clean-artifacts:
	@rm -rf $(ARTIFACTS)
