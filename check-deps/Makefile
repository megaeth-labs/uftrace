CHECK_LIST := clock_without_librt
CHECK_LIST += cc_has_mfentry
CHECK_LIST += cxa_demangle
CHECK_LIST += have_libelf
CHECK_LIST += cc_has_mno_sse2
CHECK_LIST += arm_has_hardfp
CHECK_LIST += have_libncurses
CHECK_LIST += have_libdw
CHECK_LIST += have_libunwind
CHECK_LIST += have_libcapstone
CHECK_LIST += cc_has_minline_all_stringops
CHECK_LIST += cc_has_mgeneral_regs_only

#
# This is needed for checking build dependency
#

CHECK_CFLAGS  = $(CFLAGS)  $(CFLAGS_$@) -O2 -Werror
CHECK_LDFLAGS = $(LDFLAGS) $(LDFLAGS_$@)

ifneq ($(O), )
  CHKDIR := $(O)/check-deps
else
  CHKDIR := $(CURDIR)
endif

CFLAGS_cc_has_mfentry = -mfentry
LDFLAGS_cxa_demangle = -lstdc++
LDFLAGS_have_libelf = -lelf
CFLAGS_cc_has_mno_sse2 = -mno-sse -mno-sse2
CFLAGS_have_libncurses  = $(shell pkg-config --cflags ncursesw 2> /dev/null)
LDFLAGS_have_libncurses = $(shell pkg-config --libs   ncursesw 2> /dev/null)
CFLAGS_have_libdw  = $(shell pkg-config --cflags libdw 2> /dev/null)
LDFLAGS_have_libdw = $(shell pkg-config --libs   libdw 2> /dev/null || echo "-ldw")
CFLAGS_have_libunwind  = $(shell pkg-config --cflags libunwind 2> /dev/null)
LDFLAGS_have_libunwind = $(shell pkg-config --libs   libunwind 2> /dev/null)
CFLAGS_have_libcapstone  = $(shell pkg-config --cflags capstone 2> /dev/null)
LDFLAGS_have_libcapstone = $(shell pkg-config --libs   capstone 2> /dev/null || echo "-lcapstone")
CFLAGS_cc_has_minline_all_stringops = -minline-all-stringops
CFLAGS_cc_has_mgeneral_regs_only = -mgeneral-regs-only

check-build: $(CHECK_LIST)

$(CHECK_LIST): %: __%.c $(CHKDIR)/check-tstamp
	@$(CC) $(CHECK_CFLAGS) -o $(CHKDIR)/$@  $< $(CHECK_LDFLAGS) > /dev/null 2>&1

$(CHKDIR)/check-tstamp: PHONY
	@mkdir -p $(dir $@)
	@touch $@
	@if [ `id -u` -eq 0 ]; then chmod 666 $@; fi

check-clean:
	# This is to find and remove all the executable files.
	# In overlayfs, entire files are matched with -executable option,
	# so -perm option is used instead.
	@$(RM) $(shell find $(CHKDIR) -type f -perm -u=x,g=x,o=x 2> /dev/null) $(CHKDIR)/check-tstamp $(CHKDIR)/*.o

.PHONY: PHONY;
