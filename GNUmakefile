# GNU Makefile for mgsh (Fedora Linux + GCC only)
# Supports: POSIX and ISO_C API modes
# Features: Debug/Release, sanitizers, coverage, shared libs in Debug for
# strict linking, tests via 'make check'

CC := gcc
AR := ar
RM := rm -f
RMDIR := rm -rf
MKDIR := mkdir -p

# Default target
.PHONY: all
all: mgsh

# --------------------------------------------------------------------------
# Configuration variables (override on command line if needed)
# --------------------------------------------------------------------------

# API_TYPE: POSIX, UCRT or ISO_C
API_TYPE ?= ISO_C

# BUILD_TYPE: Debug or Release
BUILD_TYPE ?= Debug

# Optional features
ENABLE_SANITIZERS ?= 0
ENABLE_COVERAGE ?= 0

# Derived flags
ifeq ($(BUILD_TYPE),Debug)
  CFLAGS_BASE := -O0 -g3
else
  CFLAGS_BASE := -O2
endif

CFLAGS_COMMON := -std=c23 -Wall

CFLAGS_EXTRA := -Wextra -Wpedantic -Wshadow -Wformat=2 \
	-Wformat-signedness -Wformat-security -Wconversion \
	-Wsign-conversion -Wnull-dereference -Wdouble-promotion \
	-Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition \
	-Wmissing-declarations -Wcast-align -Wcast-qual -Wunused \
	-Wvla -Wswitch-default -Wswitch-enum -Wfloat-equal -Wundef \
	-fanalyzer -Wlogical-op -Warith-conversion -Wduplicated-cond \
	-Wduplicated-branches -D_POSIX_C_SOURCE=202401L

# For EMACS
# CFLAGS_COMMON += -fno-diagnostics-color -fdiagnostics-urls=never

# Flags based on API type and C library
ifeq ($(API_TYPE),ISO_C)
  VISIBILITY :=
  API_DEFINE := -DISO_C_API
endif
ifeq ($(API_TYPE),UCRT)
  VISIBILITY :=
  API_DEFINE := -DUCRT_API
endif
ifeq ($(API_TYPE),POSIX)
  # FIXME: do proper visibility and declspec for share objects
  # VISIBILITY := -fvisibility=hidden
  VISIBILITY :=
  API_DEFINE := -DPOSIX_API
endif

# Debug/Release macro
ifeq ($(BUILD_TYPE),Debug)
  BUILD_DEFINE := -DMGSH_DEBUG_BUILD
else
  BUILD_DEFINE := -DMGSH_RELEASE_BUILD
endif

# Sanitizers / Coverage
ifeq ($(ENABLE_SANITIZERS),1)
  SAN_FLAGS := -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
  LDFLAGS_SAN := -fsanitize=address,undefined
endif

ifeq ($(ENABLE_COVERAGE),1)
  COV_FLAGS := -O0 --coverage
  LDFLAGS_COV := --coverage
endif

# Library type: shared in Debug (for --no-undefined checking), static in Release
# ifeq ($(BUILD_TYPE),Debug)
ifeq ($(BUILD_TYPE),XXX)
  LIB_TYPE := shared
  LIB_EXT := so
  LIB_FLAGS := -shared -Wl,--no-undefined
  PIC_FLAGS := -fPIC
else
  LIB_TYPE := static
  LIB_EXT := a
  LIB_FLAGS :=
  PIC_FLAGS :=
endif

# Final compiler flags
CFLAGS := $(CFLAGS_BASE) $(CFLAGS_COMMON) $(VISIBILITY) $(SAN_FLAGS) \
	$(COV_FLAGS) $(API_DEFINE) $(BUILD_DEFINE) -I src

LDFLAGS := $(LDFLAGS_SAN) $(LDFLAGS_COV)

# --------------------------------------------------------------------------
# Directories
# --------------------------------------------------------------------------

BUILD_DIR := build/$(BUILD_TYPE)_$(API_TYPE)
BIN_DIR := $(BUILD_DIR)/bin
LIB_DIR := $(BUILD_DIR)/lib
OBJ_DIR := $(BUILD_DIR)/obj
TEST_DIR := $(BUILD_DIR)/test

$(shell $(MKDIR) $(BIN_DIR) $(LIB_DIR) $(OBJ_DIR) $(TEST_DIR))

# --------------------------------------------------------------------------
# Source lists (mirroring the CMake structure)
# --------------------------------------------------------------------------

MGSHBASE_SOURCES := \
	src/getopt.c \
	src/lib.c \
	src/logging.c \
	src/string_t.c \
	src/xalloc.c

MGSHSTORE_SOURCES := \
	src/alias.c \
	src/alias_store.c \
	src/alias_array.c \
	src/fd_table.c \
	src/func_map.c \
	src/func_store.c \
	src/gnode.c \
	src/gprint.c \
	src/job_store.c \
	src/sig_act.c \
	src/trap_store.c \
	src/variable_map.c \
	src/variable_store.c \
	src/token.c \
	src/token_array.c \
	src/ast.c \
	src/pattern_removal.c

MGSHLOGIC_SOURCES := \
	src/arithmetic.c \
	src/lower.c \
	src/lexer.c \
	src/lexer_arith_exp.c \
	src/lexer_cmd_subst.c \
	src/lexer_heredoc.c \
	src/lexer_normal.c \
	src/lexer_squote.c \
	src/lexer_dquote.c \
	src/lexer_param_exp.c \
	src/tokenizer.c \
	src/parser.c \
	src/exec.c \
	src/expander.c \
	src/shell.c \
	src/builtins.c \
	src/positional_params.c

MAIN_SOURCE := src/main.c

CTEST_OBJ_SOURCE := test/ctest/ctest.c

# Test sources
BASE_TESTS := \
	test/mgsh/test_xalloc_ctest.c \
	test/mgsh/test_getopt_ctest.c \
	test/mgsh/test_string_ctest.c

STORE_TESTS := \
	test/mgsh/test_alias_ctest.c \
	test/mgsh/test_ast_ctest.c \
	test/mgsh/test_fd_table_ctest.c \
	test/mgsh/test_sig_act_ctest.c

LOGIC_TESTS := \
	test/mgsh/test_lexer_quotes_ctest.c \
	test/mgsh/test_lexer_param_exp_ctest.c \
	test/mgsh/test_lexer_cmd_subst_ctest.c \
	test/mgsh/test_lexer_arith_exp_ctest.c \
	test/mgsh/test_lexer_heredoc_ctest.c \
	test/mgsh/test_parser_gnode_ctest.c \
	test/mgsh/test_parser_ctest.c \
	test/mgsh/test_ast_heredoc_ctest.c \
	test/mgsh/test_tokenizer_ctest.c \
	test/mgsh/test_expander_ctest.c \
	test/mgsh/test_arithmetic_ctest.c

	# test/mgsh/test_exec_ctest.c

ALL_TEST_SOURCES := $(BASE_TESTS) $(STORE_TESTS) $(LOGIC_TESTS)

# --------------------------------------------------------------------------
# Object files
# --------------------------------------------------------------------------

BASE_OBJS := $(addprefix $(OBJ_DIR)/,$(MGSHBASE_SOURCES:.c=.o))
STORE_OBJS := $(addprefix $(OBJ_DIR)/,$(MGSHSTORE_SOURCES:.c=.o))
LOGIC_OBJS := $(addprefix $(OBJ_DIR)/,$(MGSHLOGIC_SOURCES:.c=.o))
MAIN_OBJ := $(OBJ_DIR)/src/main.o
CTEST_OBJ := $(OBJ_DIR)/test/ctest/ctest.o

# --------------------------------------------------------------------------
# Library targets
# --------------------------------------------------------------------------

BASE_LIB := $(LIB_DIR)/libmgshbase.$(LIB_EXT)
STORE_LIB := $(LIB_DIR)/libmgshstore.$(LIB_EXT)
LOGIC_LIB := $(LIB_DIR)/libmgshlogic.$(LIB_EXT)

ifeq ($(LIB_TYPE),shared)
$(BASE_LIB): $(BASE_OBJS)
	$(MKDIR) $(@D)
	$(CC) $(LIB_FLAGS) -o $@ $^ $(LDFLAGS)

$(STORE_LIB): $(STORE_OBJS) $(BASE_LIB)
	$(MKDIR) $(@D)
	$(CC) $(LIB_FLAGS) -o $@ $(STORE_OBJS) \
	  -L$(LIB_DIR) -lmgshbase $(LDFLAGS)

$(LOGIC_LIB): $(LOGIC_OBJS) $(STORE_LIB) $(BASE_LIB)
	$(MKDIR) $(@D)
	$(CC) $(LIB_FLAGS) -o $@ $(LOGIC_OBJS) \
	  -L$(LIB_DIR) -lmgshstore -lmgshbase $(LDFLAGS)
else
$(BASE_LIB): $(BASE_OBJS)
	$(MKDIR) $(@D)
	$(AR) rcs $@ $^

$(STORE_LIB): $(STORE_OBJS) $(BASE_LIB)
	$(MKDIR) $(@D)
	$(AR) rcs $@ $(STORE_OBJS)

$(LOGIC_LIB): $(LOGIC_OBJS) $(STORE_LIB) $(BASE_LIB)
	$(MKDIR) $(@D)
	$(AR) rcs $@ $(LOGIC_OBJS)
endif

# --------------------------------------------------------------------------
# Executable
# --------------------------------------------------------------------------

$(BIN_DIR)/mgsh: $(MAIN_OBJ) $(LOGIC_LIB) $(STORE_LIB) $(BASE_LIB)
	$(MKDIR) $(@D)
	$(CC) -o $@ $(MAIN_OBJ) \
	  -L$(LIB_DIR) -lmgshlogic -lmgshstore -lmgshbase $(LDFLAGS)

mgsh: $(BIN_DIR)/mgsh

# --------------------------------------------------------------------------
# Test executables
# --------------------------------------------------------------------------

# Base tests: only depend on mgshbase
define BUILD_BASE_TEST
$(TEST_DIR)/$(notdir $(basename $1)): $1 $(CTEST_OBJ) $(BASE_LIB)
	@test -n "$(@D)" && $(MKDIR) $(@D) || true
	$(CC) $(CFLAGS) $(PIC_FLAGS) -DIN_CTEST -I test/ctest -o $$@ $$< $(CTEST_OBJ) \
	  -L$(LIB_DIR) -lmgshbase $(LDFLAGS)
endef

# Store tests: depend on mgshstore and mgshbase
define BUILD_STORE_TEST
$(TEST_DIR)/$(notdir $(basename $1)): $1 $(CTEST_OBJ) $(STORE_LIB) $(BASE_LIB)
	@test -n "$(@D)" && $(MKDIR) $(@D) || true
	$(CC) $(CFLAGS) $(PIC_FLAGS) -DIN_CTEST -I test/ctest -o $$@ $$< $(CTEST_OBJ) \
	  -L$(LIB_DIR) -lmgshstore -lmgshbase $(LDFLAGS)
endef

# Logic tests: depend on all libraries
define BUILD_LOGIC_TEST
$(TEST_DIR)/$(notdir $(basename $1)): $1 $(CTEST_OBJ) $(LOGIC_LIB) $(STORE_LIB) $(BASE_LIB)
	@test -n "$(@D)" && $(MKDIR) $(@D) || true
	$(CC) $(CFLAGS) $(PIC_FLAGS) -DIN_CTEST -I test/ctest -o $$@ $$< $(CTEST_OBJ) \
	  -L$(LIB_DIR) -lmgshlogic -lmgshstore -lmgshbase $(LDFLAGS)
endef

$(foreach test,$(BASE_TESTS),$(eval $(call BUILD_BASE_TEST,$(test))))
$(foreach test,$(STORE_TESTS),$(eval $(call BUILD_STORE_TEST,$(test))))
$(foreach test,$(LOGIC_TESTS),$(eval $(call BUILD_LOGIC_TEST,$(test))))

TEST_EXES := $(addprefix $(TEST_DIR)/,$(notdir $(basename $(ALL_TEST_SOURCES))))

# --------------------------------------------------------------------------
# Compilation rules
# --------------------------------------------------------------------------

$(OBJ_DIR)/src/%.o: %.c
	$(MKDIR) $(@D)
	$(CC) $(CFLAGS) $(PIC_FLAGS) -c -o $@ $<

$(OBJ_DIR)/test/ctest/%.o: %.c
	$(MKDIR) $(@D)
	$(CC) $(CFLAGS) $(PIC_FLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: %.c
	$(MKDIR) $(@D)
	$(CC) $(CFLAGS) $(PIC_FLAGS) -c -o $@ $<

# --------------------------------------------------------------------------
# Phony targets
# --------------------------------------------------------------------------

.PHONY: clean
clean:
	$(RMDIR) $(BUILD_DIR)

.PHONY: check
check: $(TEST_EXES)
	@echo "Running tests..."
	@failed=0; \
	for test in $(TEST_EXES); do \
	  echo "=== Running $$test ==="; \
	  if ! $$test; then \
	    echo "$$test FAILED"; \
	    failed=$$((failed+1)); \
	  else \
	    echo "$$test PASSED"; \
	  fi; \
	done; \
	if [ $$failed -eq 0 ]; then \
	  echo "All tests PASSED"; \
	  exit 0; \
	else \
	  echo "$$failed test(s) FAILED"; \
	  exit 1; \
	fi

.PHONY: coverage
coverage: ENABLE_COVERAGE=1
coverage: BUILD_TYPE=Debug
coverage: all check
	@lcov --capture --directory $(BUILD_DIR) --output-file coverage.raw.info
	@lcov --remove coverage.raw.info '/usr/*' '*/test/*' '*/src/main.c' '*/src/getopt.c' \
	  --output-file coverage.info
	@lcov --extract coverage.info '*/src/*.c' --output-file coverage.filtered.info
	@genhtml coverage.filtered.info --output-directory coverage_report \
	  --title "mgsh Coverage" --branch-coverage --legend
	@echo "Coverage report generated in coverage_report/"

.PHONY: wtf
wtf:
	@echo ""
	@echo "   You've got this! Everything is going to be fine.  "
	@echo ""
	@echo "  Keep going - the code will bend to your will eventually"
	@echo "  Take a deep breath, grab a coffee, and come back stronger."
	@echo ""

# --------------------------------------------------------------------------
# Default usage hint
# --------------------------------------------------------------------------

help:
	@echo "Usage:"
	@echo "  make                    # Build mgsh (default: Release, POSIX)"
	@echo "  make BUILD_TYPE=Debug   # Debug build with shared libs"
	@echo "  make API_TYPE=ISO_C     # Strict ISO C mode"
	@echo "  make ENABLE_SANITIZERS=1# Build with ASan/UBSan"
	@echo "  make coverage           # Generate coverage report"
	@echo "  make check              # Build and run all tests"
	@echo "  make clean              # Remove build directory"
	@echo "  make wtf                # Motivational support :-)"
