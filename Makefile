VERSION := 0.17

# Makefiles suck: This macro sets a default value of $(2) for the
# variable named by $(1), unless the variable has been set by
# environment or command line. This is necessary for CC and AR
# because make sets default values, so the simpler ?= approach
# won't work as expected.
define allow-override
  $(if $(or $(findstring environment,$(origin $(1))),\
            $(findstring command line,$(origin $(1)))),,\
    $(eval $(1) = $(2)))
endef

# Allow setting CC and AR and LD, or setting CROSS_COMPILE as a prefix.
$(call allow-override,CC,$(CROSS_COMPILE)gcc)
$(call allow-override,AR,$(CROSS_COMPILE)ar)
$(call allow-override,LD,$(CROSS_COMPILE)ld)

uname_M := $(shell uname -m 2>/dev/null || echo not)

ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/i386/ -e s/arm.*/arm/ )
ifneq ($(findstring x86,$(ARCH)),)
  ifneq ($(findstring m32,$(CC) $(CFLAGS)),)
    override ARCH := i386
  endif
endif

prefix ?= /usr/local
bindir = $(prefix)/bin
libdir = $(prefix)/lib/motrace
etcdir = $(prefix)/etc
mandir = $(prefix)/share/man
docdir = $(srcdir)/doc
completiondir = $(etcdir)/bash_completion.d

ifeq ($(DOCLANG), ko)
  docdir = $(srcdir)/doc/ko
endif

srcdir = $(CURDIR)
# set objdir to $(O) by default (if any)
ifeq ($(objdir),)
    ifneq ($(O),)
        objdir = $(O)
    else
        objdir = $(CURDIR)
    endif
endif

ifneq ($(wildcard $(objdir)/.config),)
  include $(objdir)/.config
endif

RM = rm -f
INSTALL = install

export ARCH CC AR LD RM srcdir objdir LDFLAGS

COVERAGE_DIRS := $(srcdir)
ifneq ($(objdir),$(srcdir))
  COVERAGE_DIRS += $(wildcard $(objdir))
endif

COMMON_CFLAGS := -std=gnu11 -D_GNU_SOURCE $(CFLAGS) $(CPPFLAGS)
COMMON_CFLAGS += -iquote $(srcdir) -iquote $(objdir) -iquote $(srcdir)/arch/$(ARCH)
COMMON_CFLAGS += -W -Wall -Wno-unused-parameter -Wno-missing-field-initializers
COMMON_CFLAGS += -Wdeclaration-after-statement -Wstrict-prototypes

COMMON_LDFLAGS := -ldl -pthread -Wl,-z,noexecstack $(LDFLAGS)
ifeq ($(ANDROID),)
COMMON_LDFLAGS += -lrt
else
COMMON_LDFLAGS += -landroid
endif

ifneq ($(elfdir),)
  COMMON_CFLAGS  += -I$(elfdir)/include
  COMMON_LDFLAGS += -L$(elfdir)/lib
endif

#
# Note that the plain CFLAGS and LDFLAGS can be changed
# by config/Makefile later but *_*FLAGS can not.
#
MOTRACE_CFLAGS     = $(COMMON_CFLAGS) $(CFLAGS_$@) $(CFLAGS_motrace)
DEMANGLER_CFLAGS   = $(COMMON_CFLAGS) $(CFLAGS_$@) $(CFLAGS_demangler)
SYMBOLS_CFLAGS     = $(COMMON_CFLAGS) $(CFLAGS_$@) $(CFLAGS_symbols)
DBGINFO_CFLAGS     = $(COMMON_CFLAGS) $(CFLAGS_$@) $(CFLAGS_dbginfo)
BENCH_CFLAGS       = -D_GNU_SOURCE -g -pg $(CFLAGS_$@) $(CFLAGS_bench)
TRACEEVENT_CFLAGS  = $(COMMON_CFLAGS) $(CFLAGS_$@) $(CFLAGS_traceevent)
LIB_CFLAGS         = $(COMMON_CFLAGS) $(CFLAGS_$@) $(CFLAGS_lib)
LIB_CFLAGS        += -fPIC -fvisibility=hidden -fno-omit-frame-pointer
LIB_CFLAGS        += -fno-builtin -fno-tree-vectorize -DLIBMCOUNT
TEST_CFLAGS        = $(COMMON_CFLAGS) -DUNIT_TEST

MOTRACE_LDFLAGS    = $(COMMON_LDFLAGS) $(LDFLAGS_$@) $(LDFLAGS_motrace) -lm
DEMANGLER_LDFLAGS  = $(COMMON_LDFLAGS) $(LDFLAGS_$@) $(LDFLAGS_demangler)
SYMBOLS_LDFLAGS    = $(COMMON_LDFLAGS) $(LDFLAGS_$@) $(LDFLAGS_symbols)
DBGINFO_LDFLAGS    = $(COMMON_LDFLAGS) $(LDFLAGS_$@) $(LDFLAGS_dbginfo)
BENCH_LDFLAGS      = -Wl,-z,noexecstack $(LDFLAGS_$@) $(LDFLAGS_bench)
LIB_LDFLAGS        = $(COMMON_LDFLAGS) $(LDFLAGS_$@) $(LDFLAGS_lib) -Wl,--no-undefined
TEST_LDFLAGS       = $(COMMON_LDFLAGS) -lm

_DEFAULT_SANITIZERS := address,leak
ifeq ($(ARCH), riscv64)
  DEFAULT_SANITIZERS = $(_DEFAULT_SANITIZERS)
else
  DEFAULT_SANITIZERS = $(_DEFAULT_SANITIZERS),undefined
endif

ifeq ($(DEBUG), 1)
  COMMON_CFLAGS += -O0 -g3 -DDEBUG_MODE=1 -Werror
else
  COMMON_CFLAGS += -O2 -g -DDEBUG_MODE=0
endif

ifeq ($(TRACE), 1)
  TRACE_CFLAGS      := -pg -fno-omit-frame-pointer
  MOTRACE_CFLAGS    += $(TRACE_CFLAGS)
  DEMANGLER_CFLAGS  += $(TRACE_CFLAGS)
  SYMBOLS_CFLAGS    += $(TRACE_CFLAGS)
  DBGINFO_CFLAGS    += $(TRACE_CFLAGS)
  TRACEEVENT_CFLAGS += $(TRACE_CFLAGS)
  TEST_CFLAGS       += $(TRACE_CFLAGS)
  # cannot add -pg to LIB_CFLAGS because mcount() is not reentrant
endif

ifeq ($(COVERAGE), 1)
  COVERAGE_CFLAGS := -O0 -g --coverage -U_FORTIFY_SOURCE
  COMMON_CFLAGS   += $(COVERAGE_CFLAGS)
  LIB_CFLAGS      += $(COVERAGE_CFLAGS)
  TEST_CFLAGS     += $(COVERAGE_CFLAGS)

  LIB_LDFLAGS   += --coverage
endif

ifeq ($(ASAN), 1)
  ASAN_CFLAGS       := -O0 -g -fsanitize=$(DEFAULT_SANITIZERS)
  MOTRACE_CFLAGS    += $(ASAN_CFLAGS)
  DEMANGLER_CFLAGS  += $(ASAN_CFLAGS)
  SYMBOLS_CFLAGS    += $(ASAN_CFLAGS)
  DBGINFO_CFLAGS    += $(ASAN_CFLAGS)
  TRACEEVENT_CFLAGS += $(ASAN_CFLAGS)
  TEST_CFLAGS       += $(ASAN_CFLAGS)
endif

ifneq ($(SAN),)
  ifeq ($(SAN), all)
    SAN_CFLAGS := -O0 -g -fsanitize=$(DEFAULT_SANITIZERS)
  else
    SAN_CFLAGS := -O0 -g -fsanitize=$(SAN)
  endif
  MOTRACE_CFLAGS    += $(SAN_CFLAGS)
  DEMANGLER_CFLAGS  += $(SAN_CFLAGS)
  SYMBOLS_CFLAGS    += $(SAN_CFLAGS)
  DBGINFO_CFLAGS    += $(SAN_CFLAGS)
  TRACEEVENT_CFLAGS += $(SAN_CFLAGS)
  TEST_CFLAGS       += $(SAN_CFLAGS)
endif

export MOTRACE_CFLAGS LIB_CFLAGS TEST_CFLAGS TEST_LDFLAGS

VERSION_GIT := $(shell git describe --tags 2> /dev/null || echo v$(VERSION))

all:

ifneq ($(wildcard $(objdir)/check-deps/check-tstamp),)
  include $(srcdir)/check-deps/Makefile.check
endif

include $(srcdir)/Makefile.include


LIBMCOUNT_TARGETS := libmcount/libmcount.so

_TARGETS := motrace
_TARGETS += $(LIBMCOUNT_TARGETS)
_TARGETS += misc/demangler misc/symbols misc/dbginfo
TARGETS  := $(patsubst %,$(objdir)/%,$(_TARGETS))

MOTRACE_CMD_SRCS := $(wildcard $(srcdir)/cmds/*.c)
MOTRACE_UTIL_SRCS := $(wildcard $(srcdir)/utils/*.c)
MOTRACE_UTIL_SRCS := $(filter-out $(srcdir)/utils/kernel.c \
	$(srcdir)/utils/kernel-parser.c \
	$(srcdir)/utils/perf.c \
	$(srcdir)/utils/tracefs.c, \
	$(MOTRACE_UTIL_SRCS))
MOTRACE_SRCS := $(srcdir)/motrace.c $(MOTRACE_CMD_SRCS) $(MOTRACE_UTIL_SRCS)
MOTRACE_OBJS := $(patsubst $(srcdir)/%.c,$(objdir)/%.o,$(MOTRACE_SRCS))

MOTRACE_OBJS_VERSION := $(objdir)/cmds/tui.o $(objdir)/cmds/info.o

DEMANGLER_SRCS := $(srcdir)/misc/demangler.c $(srcdir)/utils/demangle.c
DEMANGLER_SRCS += $(srcdir)/utils/debug.c $(srcdir)/utils/utils.c
DEMANGLER_OBJS := $(patsubst $(srcdir)/%.c,$(objdir)/%.o,$(DEMANGLER_SRCS))

SYMBOLS_SRCS := $(srcdir)/misc/symbols.c $(srcdir)/utils/session.c
SYMBOLS_SRCS += $(srcdir)/utils/demangle.c $(srcdir)/utils/rbtree.c
SYMBOLS_SRCS += $(srcdir)/utils/utils.c $(srcdir)/utils/debug.c
SYMBOLS_SRCS += $(srcdir)/utils/filter.c $(srcdir)/utils/dwarf.c
SYMBOLS_SRCS += $(srcdir)/utils/auto-args.c $(srcdir)/utils/regs.c
SYMBOLS_SRCS += $(srcdir)/utils/argspec.c
SYMBOLS_SRCS += $(wildcard $(srcdir)/utils/symbol*.c)
SYMBOLS_OBJS := $(patsubst $(srcdir)/%.c,$(objdir)/%.o,$(SYMBOLS_SRCS))

DBGINFO_SRCS := $(srcdir)/misc/dbginfo.c $(srcdir)/utils/dwarf.c
DBGINFO_SRCS += $(srcdir)/utils/auto-args.c $(srcdir)/utils/regs.c
DBGINFO_SRCS += $(srcdir)/utils/utils.c $(srcdir)/utils/debug.c
DBGINFO_SRCS += $(srcdir)/utils/argspec.c $(srcdir)/utils/rbtree.c
DBGINFO_SRCS += $(srcdir)/utils/demangle.c $(srcdir)/utils/filter.c
DBGINFO_SRCS += $(wildcard $(srcdir)/utils/symbol*.c)
DBGINFO_OBJS := $(patsubst $(srcdir)/%.c,$(objdir)/%.o,$(DBGINFO_SRCS))

BENCH_SRCS := $(srcdir)/misc/bench.c
BENCH_OBJS := $(patsubst $(srcdir)/%.c,$(objdir)/%.o,$(BENCH_SRCS))

MOTRACE_ARCH_OBJS := $(objdir)/arch/$(ARCH)/motrace.o

MOTRACE_HDRS := $(filter-out $(srcdir)/version.h,$(wildcard $(srcdir)/*.h $(srcdir)/utils/*.h))
MOTRACE_HDRS += $(srcdir)/libmcount/mcount.h $(wildcard $(srcdir)/arch/$(ARCH)/*.h)

LIBMCOUNT_SRCS := $(filter-out %-nop.c \
	$(srcdir)/libmcount/pmu.c, \
	$(wildcard $(srcdir)/libmcount/*.c))
LIBMCOUNT_OBJS := $(patsubst $(srcdir)/%.c,$(objdir)/%.op,$(LIBMCOUNT_SRCS))

LIBMCOUNT_UTILS_SRCS += $(srcdir)/utils/debug.c $(srcdir)/utils/regs.c
LIBMCOUNT_UTILS_SRCS += $(srcdir)/utils/rbtree.c $(srcdir)/utils/filter.c
LIBMCOUNT_UTILS_SRCS += $(srcdir)/utils/demangle.c $(srcdir)/utils/utils.c
LIBMCOUNT_UTILS_SRCS += $(srcdir)/utils/auto-args.c $(srcdir)/utils/dwarf.c
LIBMCOUNT_UTILS_SRCS += $(srcdir)/utils/hashmap.c $(srcdir)/utils/argspec.c
LIBMCOUNT_UTILS_SRCS += $(srcdir)/utils/socket.c
LIBMCOUNT_UTILS_SRCS += $(srcdir)/utils/shmem.c
LIBMCOUNT_UTILS_SRCS += $(wildcard $(srcdir)/utils/symbol*.c)
LIBMCOUNT_UTILS_OBJS := $(patsubst $(srcdir)/utils/%.c,$(objdir)/libmcount/%.op,$(LIBMCOUNT_UTILS_SRCS))

LIBMCOUNT_ARCH_OBJS := $(objdir)/arch/$(ARCH)/mcount-entry.op

COMMON_DEPS := $(objdir)/.config $(MOTRACE_HDRS)
LIBMCOUNT_DEPS := $(COMMON_DEPS) $(srcdir)/libmcount/internal.h

CFLAGS_$(objdir)/mcount.op = -pthread
CFLAGS_$(objdir)/cmds/record.o = -DINSTALL_LIB_PATH='"$(libdir)"'
CFLAGS_$(objdir)/cmds/live.o = -DINSTALL_LIB_PATH='"$(libdir)"'

CFLAGS_$(objdir)/utils/demangle.o  = -Wno-unused-value
CFLAGS_$(objdir)/utils/demangle.op = -Wno-unused-value

MAKEFLAGS += --no-print-directory


all: $(objdir)/.config $(TARGETS)

$(objdir)/.config: $(srcdir)/configure $(srcdir)/check-deps/Makefile
	$(error Please run 'configure' first)

config: $(srcdir)/configure
	$(QUIET_GEN)$(srcdir)/configure --objdir=$(objdir) $(MAKEOVERRIDES)

$(LIBMCOUNT_UTILS_OBJS): $(objdir)/libmcount/%.op: $(srcdir)/utils/%.c $(LIBMCOUNT_DEPS)
	$(QUIET_CC_FPIC)$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(objdir)/libmcount/mcount.op: $(objdir)/version.h

$(LIBMCOUNT_OBJS): $(objdir)/%.op: $(srcdir)/%.c $(LIBMCOUNT_DEPS)
	$(QUIET_CC_FPIC)$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(objdir)/libmcount/libmcount.so: $(LIBMCOUNT_OBJS) $(LIBMCOUNT_UTILS_OBJS) $(LIBMCOUNT_ARCH_OBJS)
	$(QUIET_LINK)$(CC) -shared -o $@ $^ $(LIB_LDFLAGS)

$(LIBMCOUNT_ARCH_OBJS): $(wildcard $(srcdir)/arch/$(ARCH)/*.[cS]) $(LIBMCOUNT_DEPS)
	@$(MAKE) -B -C $(srcdir)/arch/$(ARCH) $@

$(MOTRACE_ARCH_OBJS): $(wildcard $(srcdir)/arch/$(ARCH)/*.[cS]) $(COMMON_DEPS)
	@$(MAKE) -B -C $(srcdir)/arch/$(ARCH) $@

$(objdir)/motrace.o: $(srcdir)/motrace.c $(objdir)/version.h $(COMMON_DEPS)
	$(QUIET_CC)$(CC) $(MOTRACE_CFLAGS) -c -o $@ $<

$(objdir)/misc/demangler.o: $(srcdir)/misc/demangler.c $(objdir)/version.h $(COMMON_DEPS)
	$(QUIET_CC)$(CC) $(DEMANGLER_CFLAGS) -c -o $@ $<

$(objdir)/misc/symbols.o: $(srcdir)/misc/symbols.c $(objdir)/version.h $(COMMON_DEPS)
	$(QUIET_CC)$(CC) $(SYMBOLS_CFLAGS) -c -o $@ $<

$(objdir)/misc/dbginfo.o: $(srcdir)/misc/dbginfo.c $(objdir)/version.h $(COMMON_DEPS)
	$(QUIET_CC)$(CC) $(DBGINFO_CFLAGS) -c -o $@ $<

$(objdir)/misc/bench.o: $(srcdir)/misc/bench.c
	$(QUIET_CC)$(CC) $(BENCH_CFLAGS) -c -o $@ $<

$(MOTRACE_OBJS_VERSION): $(objdir)/version.h

$(filter-out $(objdir)/motrace.o, $(MOTRACE_OBJS)): $(objdir)/%.o: $(srcdir)/%.c $(COMMON_DEPS)
	$(QUIET_CC)$(CC) $(MOTRACE_CFLAGS) -c -o $@ $<

$(objdir)/version.h: PHONY
	@$(srcdir)/misc/version.sh $@ $(VERSION_GIT) $(ARCH) $(objdir)

$(srcdir)/utils/auto-args.h: $(srcdir)/misc/prototypes.h $(srcdir)/misc/gen-autoargs.py
	$(QUIET_GEN)$(srcdir)/misc/gen-autoargs.py -i $< -o $@

$(objdir)/motrace: $(MOTRACE_OBJS) $(MOTRACE_ARCH_OBJS)
	$(QUIET_LINK)$(CC) $(MOTRACE_CFLAGS) -o $@ $(MOTRACE_OBJS) $(MOTRACE_ARCH_OBJS) $(MOTRACE_LDFLAGS)

$(objdir)/misc/demangler: $(DEMANGLER_OBJS)
	$(QUIET_LINK)$(CC) $(DEMANGLER_CFLAGS) -o $@ $(DEMANGLER_OBJS) $(DEMANGLER_LDFLAGS)

$(objdir)/misc/symbols: $(SYMBOLS_OBJS)
	$(QUIET_LINK)$(CC) $(SYMBOLS_CFLAGS) -o $@ $(SYMBOLS_OBJS) $(SYMBOLS_LDFLAGS)

$(objdir)/misc/dbginfo: $(DBGINFO_OBJS)
	$(QUIET_LINK)$(CC) $(DBGINFO_CFLAGS) -o $@ $(DBGINFO_OBJS) $(DBGINFO_LDFLAGS)

$(objdir)/misc/bench: $(BENCH_OBJS)
	$(QUIET_LINK)$(CC) $(BENCH_CFLAGS) -o $@ $(BENCH_OBJS) $(BENCH_LDFLAGS)


install: all
	$(Q)$(INSTALL) -d -m 755 $(DESTDIR)$(bindir)
	$(Q)$(INSTALL) -d -m 755 $(DESTDIR)$(libdir)
	$(Q)$(INSTALL) -d -m 755 $(DESTDIR)$(completiondir)
ifneq ($(wildcard $(elfdir)/lib/libelf.so),)
ifeq ($(wildcard $(prefix)/lib/libelf.so),)
	# install libelf only when it's not in the install directory.
	$(call QUIET_INSTALL, libelf)
	$(Q)$(INSTALL) $(elfdir)/lib/libelf.so   $(DESTDIR)$(libdir)/libelf.so
endif
endif
	$(call QUIET_INSTALL, motrace)
	$(Q)$(INSTALL) $(objdir)/motrace         $(DESTDIR)$(bindir)/motrace
	$(call QUIET_INSTALL, libmcount)
	$(Q)$(INSTALL) $(objdir)/libmcount/libmcount.so   $(DESTDIR)$(libdir)/libmcount.so
	$(call QUIET_INSTALL, bash-completion)
	$(Q)$(INSTALL) -m 644 $(srcdir)/misc/bash-completion.sh $(DESTDIR)$(completiondir)/motrace
	@if [ `id -u` = 0 ]; then ldconfig $(DESTDIR)$(libdir) || echo "ldconfig failed"; fi

uninstall:
	$(call QUIET_UNINSTALL, motrace)
	$(Q)$(RM) $(DESTDIR)$(bindir)/motrace
	$(call QUIET_UNINSTALL, libmcount)
	$(Q)$(RM) $(DESTDIR)$(libdir)/libmcount.so
	$(call QUIET_UNINSTALL, bash-completion)
	$(Q)$(RM) $(DESTDIR)$(completiondir)/motrace

test: all
	@$(MAKE) -C $(srcdir)/tests TESTARG="$(TESTARG)" UNITTESTARG="$(UNITTESTARG)" RUNTESTARG="$(RUNTESTARG)" PYTESTARG="$(PYTESTARG)" test

unittest: all
	@$(MAKE) -C $(srcdir)/tests TESTARG="$(TESTARG)" UNITTESTARG="$(UNITTESTARG)" test_unit

runtest: all
	@$(MAKE) -C $(srcdir)/tests TESTARG="$(TESTARG)" RUNTESTARG="$(RUNTESTARG)" test_run

bench: all $(objdir)/misc/bench
	@echo && misc/bench.sh $(BENCHARG)

dist:
	@git archive --prefix=motrace-$(VERSION)/ $(VERSION_GIT) -o $(objdir)/motrace-$(VERSION).tar
	@tar rf $(objdir)/motrace-$(VERSION).tar --transform="s|^|motrace-$(VERSION)/|" $(objdir)/version.h
	@gzip $(objdir)/motrace-$(VERSION).tar

clean:
	$(call QUIET_CLEAN, motrace)
	$(Q)$(RM) $(objdir)/*.o $(objdir)/*.op $(objdir)/*.so $(objdir)/*.a
	$(Q)$(RM) $(objdir)/cmds/*.o $(objdir)/utils/*.o $(objdir)/misc/*.o
	$(Q)$(RM) $(objdir)/utils/*.op $(objdir)/libmcount/*.op
	$(Q)$(RM) $(objdir)/utils/*.oy $(objdir)/python/*.oy
	$(Q)$(RM) $(objdir)/gmon.out $(srcdir)/scripts/*.pyc $(TARGETS)
	$(Q)$(RM) $(objdir)/motrace-*.tar.gz $(objdir)/version.h
	$(Q)find $(COVERAGE_DIRS) \( -name "*.gcda" -o -name "*.gcno" \) -type f -exec $(RM) {} +
	$(Q)$(RM) coverage.info $(C_STR_OBJS)
	@$(MAKE) -sC $(srcdir)/arch/$(ARCH) clean

reset-coverage:
	$(Q)find $(COVERAGE_DIRS) -name "*.gcda" -type f -exec $(RM) {} +
	$(Q)$(RM) coverage.info

ctags:
	@find . -name "*\.[chS]" -o -path ./tests -prune -o -path ./check-deps -prune \
		| xargs ctags --regex-asm='/^(GLOBAL|ENTRY|END)\(([^)]*)\).*/\2/'

help:
	@echo "Available targets:"
	@echo "  all           - Build motrace (default)"
	@echo "  config        - Configure motrace"
	@echo "  install       - Install built motrace"
	@echo "  uninstall     - Uninstall motrace"
	@echo "  test          - Run all tests: unit test, integration test, python test"
	@echo "  unittest      - Run unit tests"
	@echo "  runtest       - Run integration tests"
	@echo "  bench         - Run benchmark tests"
	@echo "  dist          - make *.tar file"
	@echo "  doc           - Build documentation"
	@echo "  clean         - Clean up built object files"
	@echo "  reset-coverage- Reset code coverage data (*.gcda)"
	@echo "  ctags         - Generate ctags"
	@echo "  help          - Print this help message"
	@echo ""
	@echo "Build options:"
	@echo "  make DEBUG=0|1 [targets]    - Set flags for debugging (default: 0)"
	@echo "  make TRACE=0|1 [targets]    - Set flags for tracing (default: 0)"
	@echo "  make COVERAGE=0|1 [targets] - Set flags for code coverage (default: 0)"
	@echo "  make ASAN=0|1 [targets]     - Set flags for AddressSanitizer (default: 0)"
	@echo "  make SAN=all [targets]      - Set flags for Sanitizer (default: none)"
	@echo "  make DOCLANG=ko [targets]   - Generate documentation in Korean (default: English)"
	@echo "  make V=0|1 [targets]        - Set verbose output (default: 0)"
	@echo "  make O=dir [targets]        - Set directory as objdir (default: $(srcdir))"
	@echo ""

$(C_STR_OBJS): $(objdir)/%.$(C_STR_EXTENSION): $(srcdir)/%
	$(QUIET_GEN)sed -e 's#\\#\\\\#g;s#\"#\\"#g;s#$$#\\n\"#;s#^#\"#' $< > $@

.PHONY: all config clean test dist doc ctags help PHONY
