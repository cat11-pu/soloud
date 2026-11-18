# Deterministic device/buffer fault-injection harness.
# ASan + UBSan + LSan give an independent, tool-level proof of "zero crash,
# zero leak" on top of the harness's own allocation accounting.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wshadow
SANITIZE ?= -fsanitize=address,undefined -fno-omit-frame-pointer

SRCS = \
	faulty_device.cpp \
	fault_injector.cpp \
	robust_manager.cpp \
	buggy_manager.cpp \
	test_harness.cpp \
	main.cpp

OBJS = $(SRCS:.cpp=.o)
BIN = fault_injection_test

# Real-engine integration (actual SoLoud core, NULL back-end).
REPO_ROOT = ../..
SO_INC = $(REPO_ROOT)/include
SO_CORE = $(wildcard $(REPO_ROOT)/src/core/*.cpp)
SO_NULL = $(REPO_ROOT)/src/backend/null/soloud_null.cpp
SO_NOISE = $(REPO_ROOT)/src/audiosource/noise/soloud_noise.cpp
SO_LIB = libsoloud_null.a
STRESS_BIN = soloud_stress

.PHONY: all clean test test-robust stress all-tests

# Runs everything. The model harness exits non-zero by design (it includes a
# deliberately buggy counterexample), so its status is reported separately.
all-tests: $(BIN) $(STRESS_BIN)
	@echo "### 1/2 robust manager under strict sanitizers (must be 0)"
	@$(MAKE) --no-print-directory test-robust
	@echo "### 2/2 full model harness incl. buggy counterexample (non-zero expected)"
	@ASAN_OPTIONS=detect_leaks=0 ./$(BIN) ; \
	 echo "model harness exit=$$? (1 = counterexample correctly detected)"
	@echo "### real-engine SoLoud stress"
	@$(MAKE) --no-print-directory stress

all: $(BIN)

$(SO_LIB):
	@mkdir -p .solibuild
	@for f in $(SO_CORE) $(SO_NULL) $(SO_NOISE); do \
		$(CXX) -std=c++17 -DWITH_NULL -DSOLOUD_NO_ASSERTS -I$(SO_INC) \
			-c $$f -o .solibuild/$$(basename $$f .cpp).o; \
	done
	ar rcs $(SO_LIB) .solibuild/*.o

$(STRESS_BIN): soloud_stress.cpp $(SO_LIB)
	$(CXX) $(CXXFLAGS) $(SANITIZE) -I$(SO_INC) -I. soloud_stress.cpp \
		$(SO_LIB) -lpthread -o $(STRESS_BIN)

stress: $(STRESS_BIN)
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	./$(STRESS_BIN)

$(BIN): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SANITIZE) $(SRCS) -o $(BIN)

test: $(BIN)
	# LSan stays enabled for its report but does not abort the process, because
	# the buggy manager is deliberately leaky as a counterexample. The harness's
	# own accounting independently fails the run with a non-zero exit code.
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:exitcode=0 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:exitcode=1 \
	./$(BIN)

# Strict mode: abort on any sanitizer issue. Run against the robust manager
# only (it must be provably crash- and leak-free under the sanitizers).
.PHONY: test-robust
test-robust: $(BIN)
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	./$(BIN) --robust-only

clean:
	rm -f $(OBJS) $(BIN)
	rm -rf .solibuild $(SO_LIB) $(STRESS_BIN)
