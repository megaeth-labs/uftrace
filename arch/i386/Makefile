LINKFLAGS := -r -m elf_i386 -z noexecstack

sdir := $(srcdir)/arch/i386
odir := $(objdir)/arch/i386

include $(srcdir)/Makefile.include

ARCH_ENTRY_SRC = $(wildcard $(sdir)/*.S)
ARCH_MCOUNT_SRC = $(wildcard $(sdir)/mcount-*.c)
ARCH_MOTRACE_SRC = $(sdir)/cpuinfo.c

ARCH_MCOUNT_OBJS  = $(patsubst $(sdir)/%.S,$(odir)/%.op,$(ARCH_ENTRY_SRC))
ARCH_MCOUNT_OBJS += $(patsubst $(sdir)/%.c,$(odir)/%.op,$(ARCH_MCOUNT_SRC))
ARCH_MOTRACE_OBJS = $(patsubst $(sdir)/%.c,$(odir)/%.o,$(ARCH_MOTRACE_SRC))

all: $(odir)/entry.op

$(odir)/mcount-entry.op: $(ARCH_MCOUNT_OBJS)
	$(QUIET_LINK)$(LD) $(LINKFLAGS) -o $@ $^

$(odir)/motrace.o: $(ARCH_MOTRACE_OBJS)
	$(QUIET_LINK)$(LD) $(LINKFLAGS) -o $@ $^

$(odir)/%.op: $(sdir)/%.S
	$(QUIET_ASM)$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(odir)/%.op: $(sdir)/%.c
	$(QUIET_CC_FPIC)$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(odir)/%.o: $(sdir)/%.c
	$(QUIET_CC)$(CC) $(MOTRACE_CFLAGS) -c -o $@ $<

clean:
	$(RM) $(odir)/*.op $(odir)/*.o
