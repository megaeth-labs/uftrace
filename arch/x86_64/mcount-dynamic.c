#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* This should be defined before #include "utils.h" */
#define PR_FMT "dynamic"
#define PR_DOMAIN DBG_DYNAMIC

#include "libmcount/dynamic.h"
#include "libmcount/internal.h"
#include "mcount-arch.h"
#include "utils/list.h"
#include "utils/symbol.h"
#include "utils/utils.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE MAP_FIXED
#endif

static const unsigned char fentry_nop_patt1[] = { 0x67, 0x0f, 0x1f, 0x04, 0x00 };
static const unsigned char fentry_nop_patt2[] = { 0x0f, 0x1f, 0x44, 0x00, 0x00 };
static const unsigned char patchable_gcc_nop[] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
static const unsigned char patchable_clang_nop[] = { 0x0f, 0x1f, 0x44, 0x00, 0x08 };

static const unsigned char endbr64[] = { 0xf3, 0x0f, 0x1e, 0xfa };

static const unsigned char mo_return_nop[] = {
	0x2e, 0x66, 0x0f, 0x1f, 0x84, 00, 00, 0x02, 00, 00,
};

static const unsigned char nop5[] = {
	0x0f, 0x1f, 0x44, 0x00, 0x00,
};

static const unsigned char nop6[] = {
	0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00,
};

static const unsigned char nop11[] = { 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00,
				       0x00, 0x00, 0x00, 0x66, 0x90 };

static unsigned long align_down(unsigned long val, unsigned long align)
{
	return val & ~(align - 1);
}

static unsigned long find_trampoline_page(unsigned long text_addr, unsigned long text_end)
{
	unsigned long low, high, prefer;
	unsigned long prev_end = 0;
	unsigned long candidate_before = 0;
	unsigned long candidate_after = 0;
	FILE *fp;
	char line[512];

	if (text_end > (unsigned long)INT32_MAX)
		low = text_end - (unsigned long)INT32_MAX;
	else
		low = 0;

	if (text_addr > ULONG_MAX - (unsigned long)INT32_MAX)
		high = ULONG_MAX;
	else
		high = text_addr + (unsigned long)INT32_MAX;

	if (low > high)
		return 0;

	prefer = ALIGN(text_end, PAGE_SIZE);
	if (prefer < low)
		prefer = low;

	fp = fopen("/proc/self/maps", "r");
	if (fp == NULL)
		return 0;

	while (fgets(line, sizeof(line), fp)) {
		unsigned long start, end;

		if (sscanf(line, "%lx-%lx", &start, &end) != 2)
			continue;

		if (start > prev_end) {
			unsigned long gap_start = prev_end;
			unsigned long gap_end = start;
			unsigned long gap_low = gap_start > low ? gap_start : low;
			unsigned long gap_high = gap_end < high ? gap_end : high;

			if (gap_high > gap_low && gap_high - gap_low >= PAGE_SIZE) {
				if (prefer >= gap_low && prefer + PAGE_SIZE <= gap_high) {
					candidate_after = prefer;
					break;
				}
				if (gap_low > prefer) {
					unsigned long page = ALIGN(gap_low, PAGE_SIZE);

					if (page + PAGE_SIZE <= gap_high) {
						candidate_after = page;
						break;
					}
				}
				else {
					unsigned long page = align_down(gap_high - PAGE_SIZE,
								       PAGE_SIZE);

					if (page >= gap_low)
						candidate_before = page;
				}
			}
		}

		if (end > prev_end)
			prev_end = end;
	}

	if (!candidate_after) {
		unsigned long gap_start = prev_end;
		unsigned long gap_end = high;
		unsigned long gap_low = gap_start > low ? gap_start : low;
		unsigned long gap_high = gap_end;

		if (gap_high > gap_low && gap_high - gap_low >= PAGE_SIZE) {
			if (prefer >= gap_low && prefer + PAGE_SIZE <= gap_high) {
				candidate_after = prefer;
			}
			else if (gap_low > prefer) {
				unsigned long page = ALIGN(gap_low, PAGE_SIZE);

				if (page + PAGE_SIZE <= gap_high)
					candidate_after = page;
			}
			else {
				unsigned long page = align_down(gap_high - PAGE_SIZE, PAGE_SIZE);

				if (page >= gap_low)
					candidate_before = page;
			}
		}
	}

	fclose(fp);

	if (candidate_after)
		return candidate_after;
	return candidate_before;
}

static const unsigned char xray_entry_disabled[] = { 0xeb, 0x09 };
static const unsigned char xray_exit_disabled[] = { 0xc3, 0x2e };
static const unsigned char xray_sled_pad[] = { 0x66, 0x0f, 0x1f, 0x84, 0x00,
					       0x00, 0x02, 0x00, 0x00 };
static const unsigned char xray_nop4[] = { 0x0f, 0x1f, 0x40, 0x00 };
static const unsigned char xray_nop3[] = { 0x0f, 0x1f, 0x00 };

static inline void xray_atomic_store2(void *addr, uint16_t value)
{
	__atomic_store_n((uint16_t *)addr, value, __ATOMIC_RELEASE);
}

static int prepare_xray_entry_sled(struct mcount_dynamic_info *mdi, struct xray_instr_map *xrmap)
{
	uint8_t *sled = (uint8_t *)(uintptr_t)xrmap->address;
	int64_t rel64;
	int32_t rel32;
	uint16_t head;
	uint8_t body[9];

	if (xrmap->kind != 0)
		return INSTRUMENT_SKIPPED;

	/* XRay guarantees 2-byte alignment for atomic patching; be defensive. */
	if ((uintptr_t)sled & 1) {
		pr_warn("unaligned xray entry sled: %p\n", sled);
		return INSTRUMENT_FAILED;
	}

	memcpy(&head, sled, sizeof(head));

	if (head == 0x9090) /* already enabled */
		return INSTRUMENT_SUCCESS;

	/* expect disabled entry sled: jmp +9 */
	if (memcmp(sled, xray_entry_disabled, sizeof(xray_entry_disabled)))
		return INSTRUMENT_SKIPPED;

	/* sanity check: default pad at +2 while disabled */
	if (memcmp(sled + 2, xray_sled_pad, sizeof(xray_sled_pad)))
		pr_dbg2("xray entry sled pad mismatch: %p\n", sled);

	rel64 = (int64_t)mdi->trampoline - ((int64_t)(uintptr_t)(sled + 2) + 5);
	if (rel64 < INT32_MIN || rel64 > INT32_MAX) {
		pr_warn("xray entry sled trampoline too far: sled=%p tramp=%#lx\n", sled,
			mdi->trampoline);
		return INSTRUMENT_FAILED;
	}

	rel32 = (int32_t)rel64;
	body[0] = 0xe8; /* call rel32 */
	memcpy(&body[1], &rel32, sizeof(rel32));
	memcpy(&body[5], xray_nop4, sizeof(xray_nop4));

	memcpy(sled + 2, body, sizeof(body));
	__builtin___clear_cache((char *)sled + 2, (char *)sled + 11);

	return INSTRUMENT_SUCCESS;
}

static int prepare_xray_exit_sled(struct mcount_dynamic_info *mdi, struct xray_instr_map *xrmap)
{
	uint8_t *sled = (uint8_t *)(uintptr_t)xrmap->address;
	int64_t rel64;
	int32_t rel32;
	uint16_t head;
	uint8_t body[9];

	if (xrmap->kind == 0)
		return INSTRUMENT_SKIPPED;

	/* XRay guarantees 2-byte alignment for atomic patching; be defensive. */
	if ((uintptr_t)sled & 1) {
		pr_warn("unaligned xray exit sled: %p\n", sled);
		return INSTRUMENT_FAILED;
	}

	memcpy(&head, sled, sizeof(head));

	if (head == 0x9090) /* already enabled */
		return INSTRUMENT_SUCCESS;

	/* expect disabled exit sled: ret; cs */
	if (memcmp(sled, xray_exit_disabled, sizeof(xray_exit_disabled)))
		return INSTRUMENT_SKIPPED;

	/* sanity check: default pad at +2 while disabled */
	if (memcmp(sled + 2, xray_sled_pad, sizeof(xray_sled_pad)))
		pr_dbg2("xray exit sled pad mismatch: %p\n", sled);

	rel64 = (int64_t)(mdi->trampoline + 16) - ((int64_t)(uintptr_t)(sled + 2) + 5);
	if (rel64 < INT32_MIN || rel64 > INT32_MAX) {
		pr_warn("xray exit sled trampoline too far: sled=%p tramp=%#lx\n", sled,
			mdi->trampoline + 16);
		return INSTRUMENT_FAILED;
	}

	rel32 = (int32_t)rel64;
	body[0] = 0xe8; /* call rel32 */
	memcpy(&body[1], &rel32, sizeof(rel32));
	body[5] = 0xc3; /* ret */
	memcpy(&body[6], xray_nop3, sizeof(xray_nop3));

	memcpy(sled + 2, body, sizeof(body));
	__builtin___clear_cache((char *)sled + 2, (char *)sled + 11);

	return INSTRUMENT_SUCCESS;
}

static int enable_xray_entry_sled(struct xray_instr_map *xrmap)
{
	uint8_t *sled = (uint8_t *)(uintptr_t)xrmap->address;
	uint16_t head;

	if (xrmap->kind != 0)
		return INSTRUMENT_SKIPPED;

	if ((uintptr_t)sled & 1) {
		pr_warn("unaligned xray entry sled: %p\n", sled);
		return INSTRUMENT_FAILED;
	}

	memcpy(&head, sled, sizeof(head));

	if (head == 0x9090) /* already enabled */
		return INSTRUMENT_SUCCESS;

	if (head != 0x09eb)
		return INSTRUMENT_SKIPPED;

	__atomic_thread_fence(__ATOMIC_RELEASE);
	xray_atomic_store2(sled, 0x9090);
	__builtin___clear_cache((char *)sled, (char *)sled + 2);

	return INSTRUMENT_SUCCESS;
}

static int enable_xray_exit_sled(struct xray_instr_map *xrmap)
{
	uint8_t *sled = (uint8_t *)(uintptr_t)xrmap->address;
	uint16_t head;

	if (xrmap->kind == 0)
		return INSTRUMENT_SKIPPED;

	if ((uintptr_t)sled & 1) {
		pr_warn("unaligned xray exit sled: %p\n", sled);
		return INSTRUMENT_FAILED;
	}

	memcpy(&head, sled, sizeof(head));

	if (head == 0x9090) /* already enabled */
		return INSTRUMENT_SUCCESS;

	if (head != 0x2ec3)
		return INSTRUMENT_SKIPPED;

	__atomic_thread_fence(__ATOMIC_RELEASE);
	xray_atomic_store2(sled, 0x9090);
	__builtin___clear_cache((char *)sled, (char *)sled + 2);

	return INSTRUMENT_SUCCESS;
}

static int disable_xray_entry_sled(struct xray_instr_map *xrmap)
{
	uint8_t *sled = (uint8_t *)(uintptr_t)xrmap->address;
	uint16_t head;

	if (xrmap->kind != 0)
		return INSTRUMENT_SKIPPED;

	if ((uintptr_t)sled & 1) {
		pr_warn("unaligned xray entry sled: %p\n", sled);
		return INSTRUMENT_FAILED;
	}

	memcpy(&head, sled, sizeof(head));

	if (head == 0x09eb) /* already disabled */
		return INSTRUMENT_SUCCESS;

	if (head != 0x9090) /* unknown state */
		return INSTRUMENT_SKIPPED;

	__atomic_thread_fence(__ATOMIC_RELEASE);
	xray_atomic_store2(sled, 0x09eb); /* eb 09 */
	__builtin___clear_cache((char *)sled, (char *)sled + 2);

	return INSTRUMENT_SUCCESS;
}

static int disable_xray_exit_sled(struct xray_instr_map *xrmap)
{
	uint8_t *sled = (uint8_t *)(uintptr_t)xrmap->address;
	uint16_t head;

	if (xrmap->kind == 0)
		return INSTRUMENT_SKIPPED;

	if ((uintptr_t)sled & 1) {
		pr_warn("unaligned xray exit sled: %p\n", sled);
		return INSTRUMENT_FAILED;
	}

	memcpy(&head, sled, sizeof(head));

	if (head == 0x2ec3) /* already disabled */
		return INSTRUMENT_SUCCESS;

	if (head != 0x9090) /* unknown state */
		return INSTRUMENT_SKIPPED;

	__atomic_thread_fence(__ATOMIC_RELEASE);
	xray_atomic_store2(sled, 0x2ec3); /* c3 2e */
	__builtin___clear_cache((char *)sled, (char *)sled + 2);

	return INSTRUMENT_SUCCESS;
}

int mcount_setup_trampoline(struct mcount_dynamic_info *mdi)
{
	unsigned char trampoline[] = { 0x3e, 0xff, 0x25, 0x01, 0x00, 0x00, 0x00, 0xcc };
	unsigned long fentry_addr = (unsigned long)__fentry__;
	unsigned long xray_entry_addr = (unsigned long)__xray_entry;
	unsigned long xray_exit_addr = (unsigned long)__xray_exit;
	unsigned long mo_entry_addr = (unsigned long)__mo_entry__;
	unsigned long mo_exit_addr = (unsigned long)__mo_exit__;

	size_t trampoline_size = 16;
	void *trampoline_check;
	unsigned long text_end;
	bool trampoline_mapped = false;

	if (mdi->type == DYNAMIC_XRAY)
		trampoline_size *= 2;

	/* find unused 16-byte at the end of the code segment */
	text_end = mdi->text_addr + mdi->text_size;
	mdi->trampoline = ALIGN(text_end, PAGE_SIZE);
	mdi->trampoline -= trampoline_size;

	if (unlikely(mdi->trampoline < text_end)) {
		unsigned long tramp_page = find_trampoline_page(mdi->text_addr, text_end);

		if (tramp_page == 0)
			pr_err("could not find trampoline gap near %#lx\n", text_end);

		pr_dbg2("adding a page for fentry trampoline at %#lx\n", tramp_page);

		trampoline_check = mmap((void *)tramp_page, PAGE_SIZE,
					PROT_READ | PROT_WRITE | PROT_EXEC,
					MAP_FIXED_NOREPLACE | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (trampoline_check != (void *)tramp_page) {
			pr_err("could not map trampoline at desired location %#lx, got %#lx: %m\n",
			       tramp_page, (uintptr_t)trampoline_check);
		}

		mdi->trampoline = tramp_page;
		trampoline_mapped = true;
	}

	if (mprotect(PAGE_ADDR(mdi->text_addr), PAGE_LEN(mdi->text_addr, mdi->text_size),
		     PROT_READ | PROT_WRITE | PROT_EXEC)) {
		pr_dbg("cannot setup trampoline due to protection: %m\n");
		return -1;
	}

	if (mdi->type == DYNAMIC_XRAY) {
		/* jmpq  *0x1(%rip)     # <mo_entry_addr> */
		memcpy((void *)mdi->trampoline, trampoline, sizeof(trampoline));
		memcpy((void *)mdi->trampoline + sizeof(trampoline), &mo_entry_addr,
		       sizeof(mo_entry_addr));

		/* jmpq  *0x1(%rip)     # <mo_exit_addr> */
		memcpy((void *)mdi->trampoline + 16, trampoline, sizeof(trampoline));
		memcpy((void *)mdi->trampoline + 16 + sizeof(trampoline), &mo_exit_addr,
		       sizeof(mo_exit_addr));
	}
	else if (mdi->type == DYNAMIC_FENTRY_NOP || mdi->type == DYNAMIC_PATCHABLE) {
		/* jmpq  *0x1(%rip)     # <fentry_addr> */
		memcpy((void *)mdi->trampoline, trampoline, sizeof(trampoline));
		memcpy((void *)mdi->trampoline + sizeof(trampoline), &fentry_addr,
		       sizeof(fentry_addr));
	}
	else if (mdi->type == DYNAMIC_NONE) {
#ifdef HAVE_LIBCAPSTONE
		unsigned long dentry_addr = (unsigned long)__dentry__;

		/* jmpq  *0x2(%rip)     # <dentry_addr> */
		memcpy((void *)mdi->trampoline, trampoline, sizeof(trampoline));
		memcpy((void *)mdi->trampoline + sizeof(trampoline), &dentry_addr,
		       sizeof(dentry_addr));
#endif
	}

	if (trampoline_mapped) {
		if (mprotect((void *)mdi->trampoline, PAGE_SIZE, PROT_READ | PROT_EXEC))
			pr_err("cannot protect trampoline page at %#lx: %m\n", mdi->trampoline);
	}
	return 0;
}

void mcount_cleanup_trampoline(struct mcount_dynamic_info *mdi)
{
	if (mprotect(PAGE_ADDR(mdi->text_addr), PAGE_LEN(mdi->text_addr, mdi->text_size),
		     PROT_READ | PROT_EXEC))
		pr_err("cannot restore trampoline due to protection");
}

static void read_xray_map(struct mcount_dynamic_info *mdi, struct motrace_elf_data *elf,
			  struct motrace_elf_iter *iter, unsigned long offset)
{
	struct xray_instr_map *xrmap;
	unsigned i;
	typeof(iter->shdr) *shdr = &iter->shdr;

	mdi->nr_patch_target = shdr->sh_size / sizeof(*xrmap);
	mdi->patch_target = xmalloc(mdi->nr_patch_target * sizeof(*xrmap));

	elf_get_secdata(elf, iter);
	elf_read_secdata(elf, iter, 0, mdi->patch_target, shdr->sh_size);

	for (i = 0; i < mdi->nr_patch_target; i++) {
		xrmap = &((struct xray_instr_map *)mdi->patch_target)[i];

		if (xrmap->version == 2) {
			xrmap->address += offset + (shdr->sh_offset + i * sizeof(*xrmap));
			xrmap->function += offset + (shdr->sh_offset + i * sizeof(*xrmap) + 8);
		}
		else if (elf->ehdr.e_type == ET_DYN) {
			xrmap->address += offset;
			xrmap->function += offset;
		}
	}
}

struct xray_patt_list {
	struct list_head list;
	bool positive;
	char *module;
	struct motrace_pattern patt;
};

static LIST_HEAD(xray_patterns);

static void xray_parse_pattern_list(const char *patch_funcs, const char *def_mod,
				    enum motrace_pattern_type ptype)
{
	struct strv funcs = STRV_INIT;
	char *name;
	int j;
	struct xray_patt_list *pl;

	if (patch_funcs == NULL || patch_funcs[0] == '\0')
		return;

	strv_split(&funcs, patch_funcs, ";");

	strv_for_each(&funcs, name, j) {
		char *delim;

		pl = xzalloc(sizeof(*pl));

		if (name[0] == '!')
			name++;
		else
			pl->positive = true;

		delim = strchr(name, '@');
		if (delim == NULL) {
			pl->module = xstrdup(def_mod);
		}
		else {
			*delim = '\0';
			pl->module = xstrdup(++delim);
		}

		init_filter_pattern(ptype, &pl->patt, name);
		list_add_tail(&pl->list, &xray_patterns);
	}

	strv_free(&funcs);
}

static void xray_release_pattern_list(void)
{
	struct xray_patt_list *pl, *tmp;

	list_for_each_entry_safe(pl, tmp, &xray_patterns, list) {
		list_del(&pl->list);
		free_filter_pattern(&pl->patt);
		free(pl->module);
		free(pl);
	}
}

static int xray_match_pattern_list(const char *libname, const char *soname, const char *sym_name)
{
	struct xray_patt_list *pl;
	int ret = 0;

	list_for_each_entry(pl, &xray_patterns, list) {
		int len = strlen(pl->module);

		if (strncmp(libname, pl->module, len) &&
		    (!soname || strncmp(soname, pl->module, len)))
			continue;

		if (match_filter_pattern(&pl->patt, (char *)sym_name))
			ret = pl->positive ? 1 : -1;
	}

	return ret;
}

struct xray_patch_ctx {
	bool enable;
	bool use_filter;
	int patched;
	int failed;
	int skipped;
};

static struct mcount_dynamic_info *create_mdi_for_phdr(struct dl_phdr_info *info)
{
	struct mcount_dynamic_info *mdi;
	bool base_addr_set = false;
	unsigned i;

	mdi = xzalloc(sizeof(*mdi));
	mdi->type = DYNAMIC_XRAY;
	INIT_LIST_HEAD(&mdi->bad_syms);

	for (i = 0; i < info->dlpi_phnum; i++) {
		const ElfW(Phdr) *phdr = &info->dlpi_phdr[i];

		if (phdr->p_type != PT_LOAD)
			continue;

		if (!base_addr_set) {
			mdi->base_addr = phdr->p_vaddr;
			base_addr_set = true;
		}

		if (!(phdr->p_flags & PF_X))
			continue;

		mdi->text_addr = phdr->p_vaddr;
		mdi->text_size = phdr->p_memsz;
		break;
	}

	mdi->base_addr += info->dlpi_addr;
	mdi->text_addr += info->dlpi_addr;

	return mdi;
}

static bool *build_xray_patch_mask(struct mcount_dynamic_info *mdi, const char *libname,
				   const char *soname)
{
	bool *mask;
	unsigned i;

	if (mdi->nr_patch_target == 0)
		return NULL;

	mask = xcalloc(mdi->nr_patch_target, sizeof(*mask));
	for (i = 0; i < mdi->nr_patch_target; i++) {
		struct xray_instr_map *xrmap = &((struct xray_instr_map *)mdi->patch_target)[i];
		struct motrace_symbol *sym = find_symtabs(&mcount_sym_info, xrmap->function);

		if (sym == NULL)
			continue;
		if (xray_match_pattern_list(libname, soname, sym->name) == 1)
			mask[i] = true;
	}

	return mask;
}

static int patch_xray_module(struct dl_phdr_info *info, size_t sz, void *data)
{
	struct xray_patch_ctx *ctx = data;
	struct mcount_dynamic_info *mdi;
	struct motrace_elf_data elf;
	struct motrace_elf_iter iter;
	const char *path = info->dlpi_name;
	const char *libname;
	char *soname = NULL;
	bool *do_patch = NULL;
	bool is_main = false;
	bool found = false;
	unsigned i;

	(void)sz;

	if (!path || !*path) {
		path = mcount_exename;
		is_main = true;
	}

	if (!path || !*path)
		return 0;

	libname = motrace_basename(path);

	mdi = create_mdi_for_phdr(info);
	if (mdi == NULL)
		return 0;

	/* Skip main executable if it's not a real file (should not happen). */
	if (is_main && access(path, F_OK) != 0)
		goto out_free;

	if (elf_init(path, &elf) < 0)
		goto out_free;

	elf_for_each_shdr(&elf, &iter) {
		char *shstr = elf_get_name(&elf, &iter, iter.shdr.sh_name);

		if (!strcmp(shstr, XRAY_SECT)) {
			read_xray_map(mdi, &elf, &iter, mdi->base_addr);
			found = true;
			break;
		}
	}

	elf_finish(&elf);

	if (!found)
		goto out_free;

	if (mcount_setup_trampoline(mdi) < 0) {
		ctx->failed++;
		goto out_free_targets;
	}

	if (ctx->use_filter) {
		soname = get_soname(path);
		do_patch = build_xray_patch_mask(mdi, libname, soname);
	}

	if (ctx->enable) {
		for (i = 0; i < mdi->nr_patch_target; i++) {
			struct xray_instr_map *xrmap =
				&((struct xray_instr_map *)mdi->patch_target)[i];
			int r;

			if (do_patch && !do_patch[i])
				continue;

			r = prepare_xray_entry_sled(mdi, xrmap);
			if (r == INSTRUMENT_SUCCESS)
				ctx->patched++;
			else if (r == INSTRUMENT_SKIPPED)
				ctx->skipped++;
			else
				ctx->failed++;

			r = prepare_xray_exit_sled(mdi, xrmap);
			if (r == INSTRUMENT_SUCCESS)
				ctx->patched++;
			else if (r == INSTRUMENT_SKIPPED)
				ctx->skipped++;
			else
				ctx->failed++;
		}

		/* flip to enabled state (2-byte atomic) */
		for (i = 0; i < mdi->nr_patch_target; i++) {
			struct xray_instr_map *xrmap =
				&((struct xray_instr_map *)mdi->patch_target)[i];
			int r;

			if (do_patch && !do_patch[i])
				continue;

			r = enable_xray_exit_sled(xrmap);
			if (r == INSTRUMENT_SUCCESS)
				ctx->patched++;
			else if (r == INSTRUMENT_SKIPPED)
				ctx->skipped++;
			else
				ctx->failed++;
		}

		for (i = 0; i < mdi->nr_patch_target; i++) {
			struct xray_instr_map *xrmap =
				&((struct xray_instr_map *)mdi->patch_target)[i];
			int r;

			if (do_patch && !do_patch[i])
				continue;

			r = enable_xray_entry_sled(xrmap);
			if (r == INSTRUMENT_SUCCESS)
				ctx->patched++;
			else if (r == INSTRUMENT_SKIPPED)
				ctx->skipped++;
			else
				ctx->failed++;
		}
	}
	else {
		/* flip to disabled state (2-byte atomic) */
		for (i = 0; i < mdi->nr_patch_target; i++) {
			struct xray_instr_map *xrmap =
				&((struct xray_instr_map *)mdi->patch_target)[i];
			int r;

			if (do_patch && !do_patch[i])
				continue;

			r = disable_xray_exit_sled(xrmap);
			if (r == INSTRUMENT_SUCCESS)
				ctx->patched++;
			else if (r == INSTRUMENT_SKIPPED)
				ctx->skipped++;
			else
				ctx->failed++;
		}

		for (i = 0; i < mdi->nr_patch_target; i++) {
			struct xray_instr_map *xrmap =
				&((struct xray_instr_map *)mdi->patch_target)[i];
			int r;

			if (do_patch && !do_patch[i])
				continue;

			r = disable_xray_entry_sled(xrmap);
			if (r == INSTRUMENT_SUCCESS)
				ctx->patched++;
			else if (r == INSTRUMENT_SKIPPED)
				ctx->skipped++;
			else
				ctx->failed++;
		}
	}

	mcount_cleanup_trampoline(mdi);

out_free_targets:
	free(do_patch);
	free(soname);
	free(mdi->patch_target);
out_free:
	free(mdi);
	return 0;
}

int mcount_mo_xray_patch(bool enable)
{
	struct xray_patch_ctx ctx = {
		.enable = enable,
	};
	const char *def_mod = "unknown";

	if (mcount_mo_patch && mcount_mo_patch[0]) {
		if (mcount_sym_info.exec_map && mcount_sym_info.exec_map->libname)
			def_mod = motrace_basename(mcount_sym_info.exec_map->libname);
		else if (mcount_exename)
			def_mod = motrace_basename(mcount_exename);

		xray_parse_pattern_list(mcount_mo_patch, def_mod, mcount_mo_patch_ptype);
		ctx.use_filter = true;
	}

	dl_iterate_phdr(patch_xray_module, &ctx);

	if (ctx.use_filter)
		xray_release_pattern_list();

	if (ctx.failed)
		return -1;
	return 0;
}

static void read_mcount_loc(struct mcount_dynamic_info *mdi, struct motrace_elf_data *elf,
			    struct motrace_elf_iter *iter, unsigned long offset)
{
	typeof(iter->shdr) *shdr = &iter->shdr;

	mdi->nr_patch_target = shdr->sh_size / sizeof(long);
	mdi->patch_target = xmalloc(shdr->sh_size);

	elf_get_secdata(elf, iter);
	elf_read_secdata(elf, iter, 0, mdi->patch_target, shdr->sh_size);

	/* symbol has relative address, fix it to match each other */
	if (elf->ehdr.e_type == ET_EXEC) {
		unsigned long *mcount_loc = mdi->patch_target;
		unsigned i;

		for (i = 0; i < mdi->nr_patch_target; i++) {
			mcount_loc[i] -= offset;
		}
	}
}

static void read_patchable_loc(struct mcount_dynamic_info *mdi, struct motrace_elf_data *elf,
			       struct motrace_elf_iter *iter, unsigned long offset)
{
	typeof(iter->shdr) *shdr = &iter->shdr;
	unsigned i;
	unsigned long *patchable_loc;
	unsigned long sh_addr;

	mdi->nr_patch_target = shdr->sh_size / sizeof(long);
	mdi->patch_target = xmalloc(shdr->sh_size);
	patchable_loc = mdi->patch_target;

	sh_addr = shdr->sh_addr;
	if (elf->ehdr.e_type == ET_DYN)
		sh_addr += offset;

	for (i = 0; i < mdi->nr_patch_target; i++) {
		unsigned long *entry = (unsigned long *)sh_addr + i;
		patchable_loc[i] = *entry - offset;
	}
}

void mcount_arch_find_module(struct mcount_dynamic_info *mdi, struct motrace_symtab *symtab)
{
	struct motrace_elf_data elf;
	struct motrace_elf_iter iter;
	unsigned i = 0;

	mdi->type = DYNAMIC_NONE;

	if (elf_init(mdi->map->libname, &elf) < 0)
		goto out;

	elf_for_each_shdr(&elf, &iter) {
		char *shstr = elf_get_name(&elf, &iter, iter.shdr.sh_name);

		if (!strcmp(shstr, PATCHABLE_SECT)) {
			mdi->type = DYNAMIC_PATCHABLE;
			read_patchable_loc(mdi, &elf, &iter, mdi->base_addr);
			goto out;
		}

		if (!strcmp(shstr, XRAY_SECT)) {
			mdi->type = DYNAMIC_XRAY;
			//			read_xray_map(mdi, &elf, &iter, mdi->base_addr);
			goto out;
		}

		if (!strcmp(shstr, MCOUNTLOC_SECT)) {
			read_mcount_loc(mdi, &elf, &iter, mdi->base_addr);
			/* still needs to check pg or fentry */
		}
	}

	/*
	 * check first few functions have fentry or patchable function entry
	 * signature.
	 */
	for (i = 0; i < symtab->nr_sym; i++) {
		struct motrace_symbol *sym = &symtab->sym[i];
		void *code_addr = (void *)sym->addr + mdi->map->start;

		if (sym->type != ST_LOCAL_FUNC && sym->type != ST_GLOBAL_FUNC)
			continue;

		/* don't check special functions */
		if (sym->name[0] == '_')
			continue;

		/*
		 * there might be some chances of not having patchable section
		 * '__patchable_function_entries' but shows the NOPs pattern.
		 * this can be treated as DYNAMIC_FENTRY_NOP.
		 */
		if (!memcmp(code_addr, patchable_gcc_nop, CALL_INSN_SIZE) ||
		    !memcmp(code_addr, patchable_clang_nop, CALL_INSN_SIZE)) {
			mdi->type = DYNAMIC_FENTRY_NOP;
			goto out;
		}

		/* only support calls to __fentry__ at the beginning */
		if (!memcmp(code_addr, fentry_nop_patt1, CALL_INSN_SIZE) ||
		    !memcmp(code_addr, fentry_nop_patt2, CALL_INSN_SIZE)) {
			mdi->type = DYNAMIC_FENTRY_NOP;
			goto out;
		}
	}

	switch (check_trace_functions(mdi->map->libname)) {
	case TRACE_MCOUNT:
		mdi->type = DYNAMIC_PG;
		break;
	case TRACE_FENTRY:
		mdi->type = DYNAMIC_FENTRY;
		break;
	default:
		break;
	}

out:
	pr_dbg("dynamic patch type: %s: %d (%s)\n", motrace_basename(mdi->map->libname), mdi->type,
	       mdi_type_names[mdi->type]);

	elf_finish(&elf);
}

static unsigned long get_target_addr(struct mcount_dynamic_info *mdi, unsigned long addr)
{
	return mdi->trampoline - (addr + CALL_INSN_SIZE);
}

static int patch_mo_return_code(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym)
{
	csh handle;
	cs_insn *insn;
	size_t i, count;
	uint32_t target_addr;
	unsigned char *code = (void *)sym->addr + mdi->map->start;

	if (cs_open(CS_ARCH_X86, CS_MODE_64, &handle) != CS_ERR_OK) {
		pr_err("Failed to open Capstone engine: %s\n", cs_strerror(cs_errno(handle)));
		return INSTRUMENT_FAILED;
	}
	count = cs_disasm(handle, code, sym->size, (uintptr_t)code, 0, &insn);
	if (count == 0) {
		cs_close(&handle);
		pr_err("Failed to disassemble code: %s\n", cs_strerror(cs_errno(handle)));
		return INSTRUMENT_FAILED;
	}
	for (i = 0; i < count; i++) {
		if (insn[i].id == X86_INS_NOP) {
			unsigned char *jmp_addr = (unsigned char *)insn[i].address;
			if (memcmp(jmp_addr, nop11, sizeof(nop11)) == 0) {
				target_addr =
					(uint32_t)get_target_addr(mdi, (unsigned long)jmp_addr) +
					16;
				if (target_addr == 0) {
					cs_free(insn, count);
					cs_close(&handle);
					pr_err("Failed to get target address\n");
					return INSTRUMENT_SKIPPED;
				}
				/* make a "call" insn with 4-byte offset */
				jmp_addr[0] = 0xe8;
				/* hopefully we're not patching 'memcpy' itself */
				memcpy(&jmp_addr[1], &target_addr, sizeof(target_addr));
				memcpy(&jmp_addr[5], nop6, sizeof(nop6));
				__builtin___clear_cache((char *)jmp_addr, (char *)jmp_addr + 11);
			}
		}
		else if (insn[i].id == X86_INS_RET) {
			unsigned char *ret_addr = (unsigned char *)insn[i].address;
			if (memcmp(ret_addr + 1, mo_return_nop, sizeof(mo_return_nop)) == 0) {
				target_addr =
					(uint32_t)get_target_addr(mdi, (unsigned long)ret_addr) +
					16;
				if (target_addr == 0) {
					cs_free(insn, count);
					cs_close(&handle);
					pr_err("Failed to get target address\n");
					return INSTRUMENT_SKIPPED;
				}
				/* make a "call" insn with 4-byte offset */
				ret_addr[0] = 0xe8;
				/* hopefully we're not patching 'memcpy' itself */
				memcpy(&ret_addr[1], &target_addr, sizeof(target_addr));
				ret_addr[5] = 0xc3;
				memcpy(&ret_addr[6], nop5, sizeof(nop5));
				__builtin___clear_cache((char *)ret_addr, (char *)ret_addr + 11);
			}
			else {
				pr_err("Failed to find patchable return instruction\n");
			}
		}
	}
	cs_free(insn, count);
	cs_close(&handle);
	return INSTRUMENT_SUCCESS;
}

static int patch_mo_code(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym)
{
	uint32_t target_addr;
	unsigned char *insn = (void *)sym->addr + mdi->map->start;

	/* skip 'endbr64' instruction, which is inserted by (implicit) -fcf-protection option. */
	if (!memcmp(insn, endbr64, sizeof(endbr64)))
		insn += sizeof(endbr64);

	/* get the jump offset to the trampoline */
	target_addr = get_target_addr(mdi, (unsigned long)insn);
	if (target_addr == 0)
		return INSTRUMENT_SKIPPED;

	/* make a "call" insn with 4-byte offset */
	insn[0] = 0xe8;
	/* hopefully we're not patching 'memcpy' itself */
	memcpy(&insn[1], &target_addr, sizeof(target_addr));

	memcpy(&insn[5], nop6, sizeof(nop6));

	__builtin___clear_cache((char *)insn, (char *)insn + 11);

	pr_dbg3("update %p for '%s' function dynamically to call __mo_entry__ and __mo_exit__\n",
		insn, sym->name);

	if (patch_mo_return_code(mdi, sym) != INSTRUMENT_SUCCESS) {
		pr_err("Failed to patch return code\n");
		return INSTRUMENT_FAILED;
	}
	return INSTRUMENT_SUCCESS;
}

static int patch_fentry_code(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym)
{
	unsigned char *insn = (void *)sym->addr + mdi->map->start;
	unsigned int target_addr;

	/* skip 'endbr64' instruction, which is inserted by (implicit) -fcf-protection option. */
	if (!memcmp(insn, endbr64, sizeof(endbr64)))
		insn += sizeof(endbr64);

	/* support patchable function entry and __fentry__ at the beginning */
	if (memcmp(insn, patchable_gcc_nop, sizeof(patchable_gcc_nop)) &&
	    memcmp(insn, patchable_clang_nop, sizeof(patchable_clang_nop)) &&
	    memcmp(insn, fentry_nop_patt1, sizeof(fentry_nop_patt1)) &&
	    memcmp(insn, fentry_nop_patt2, sizeof(fentry_nop_patt2))) {
		pr_dbg4("skip non-applicable functions: %s\n", sym->name);
		return INSTRUMENT_SKIPPED;
	}

	/* get the jump offset to the trampoline */
	target_addr = get_target_addr(mdi, (unsigned long)insn);
	if (target_addr == 0)
		return INSTRUMENT_SKIPPED;

	/* make a "call" insn with 4-byte offset */
	insn[0] = 0xe8;
	/* hopefully we're not patching 'memcpy' itself */
	memcpy(&insn[1], &target_addr, sizeof(target_addr));

	pr_dbg3("update %p for '%s' function dynamically to call __fentry__\n", insn, sym->name);

	return INSTRUMENT_SUCCESS;
}

static int patch_fentry_func(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym)
{
	return patch_fentry_code(mdi, sym);
}

static int patch_patchable_func(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym)
{
	/* it does the same patch logic with fentry. */
	return patch_fentry_code(mdi, sym);
}

static int patch_mo_func(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym)
{
	return patch_mo_code(mdi, sym);
}

static int update_xray_code(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym,
			    struct xray_instr_map *xrmap)
{
	unsigned char entry_insn[] = { 0xeb, 0x09 };
	unsigned char exit_insn[] = { 0xc3, 0x2e };
	unsigned char pad[] = { 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x02, 0x00, 0x00 };
	unsigned char nop6[] = { 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00 };
	unsigned char nop4[] = { 0x0f, 0x1f, 0x40, 0x00 };
	unsigned int target_addr;
	unsigned char *func = (void *)xrmap->address;
	union {
		unsigned long word;
		char bytes[8];
	} patch;

	if (memcmp(func + 2, pad, sizeof(pad)))
		return INSTRUMENT_FAILED;

	if (xrmap->kind == 0) { /* ENTRY */
		if (memcmp(func, entry_insn, sizeof(entry_insn)))
			return INSTRUMENT_FAILED;

		target_addr = mdi->trampoline - (xrmap->address + 5);

		memcpy(func + 5, nop6, sizeof(nop6));

		/* need to write patch_word atomically */
		patch.bytes[0] = 0xe8; /* "call" insn */
		memcpy(&patch.bytes[1], &target_addr, sizeof(target_addr));
		memcpy(&patch.bytes[5], nop6, 3);

		memcpy(func, patch.bytes, sizeof(patch));
	}
	else { /* EXIT */
		if (memcmp(func, exit_insn, sizeof(exit_insn)))
			return INSTRUMENT_FAILED;

		target_addr = mdi->trampoline + 16 - (xrmap->address + 5);

		memcpy(func + 5, nop4, sizeof(nop4));

		/* need to write patch_word atomically */
		patch.bytes[0] = 0xe9; /* "jmp" insn */
		memcpy(&patch.bytes[1], &target_addr, sizeof(target_addr));
		memcpy(&patch.bytes[5], nop4, 3);

		memcpy(func, patch.bytes, sizeof(patch));
	}

	pr_dbg3("update %p for '%s' function %s dynamically to call xray functions\n", func,
		sym->name, xrmap->kind == 0 ? "entry" : "exit ");
	return INSTRUMENT_SUCCESS;
}

static int patch_xray_func(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym)
{
	unsigned i;
	int ret = -2;
	struct xray_instr_map *xrmap;
	uint64_t sym_addr = sym->addr + mdi->map->start;

	/* xray provides a pair of entry and exit (or more) */
	for (i = 0; i < mdi->nr_patch_target; i++) {
		xrmap = &((struct xray_instr_map *)mdi->patch_target)[i];

		if (xrmap->address < sym_addr || xrmap->address >= sym_addr + sym->size)
			continue;

		while ((ret = update_xray_code(mdi, sym, xrmap)) == 0) {
			if (i == mdi->nr_patch_target - 1)
				break;
			i++;

			if (xrmap->function != xrmap[1].function)
				break;
			xrmap++;
		}

		break;
	}

	return ret;
}

/*
 *  we overwrite instructions over 5bytes from start of function
 *  to call '__dentry__' that seems similar like '__fentry__'.
 *
 *  while overwriting, After adding the generated instruction which
 *  returns to the address of the original instruction end,
 *  save it in the heap.
 *
 *  for example:
 *
 *   4005f0:       31 ed                   xor     %ebp,%ebp
 *   4005f2:       49 89 d1                mov     %rdx,%r9
 *   4005f5:       5e                      pop     %rsi
 *
 *  will changed like this :
 *
 *   4005f0	call qword ptr [rip + 0x200a0a] # 0x601000
 *
 *  and keeping original instruction :
 *
 *  Original Instructions---------------
 *    f1cff0:	xor ebp, ebp
 *    f1cff2:	mov r9, rdx
 *    f1cff5:	pop rsi
 *  Generated Instruction to return-----
 *    f1cff6:	jmp qword ptr [rip]
 *    f1cffc:	QW 0x00000000004005f6
 *
 *  In the original case, address 0x601000 has a dynamic symbol
 *  start address. It is also the first element in the GOT array.
 *  while initializing the mcount library, we will replace it with
 *  the address of the function '__dentry__'. so, the changed
 *  instruction will be calling '__dentry__'.
 *
 *  '__dentry__' has a similar function like '__fentry__'.
 *  the other thing is that it returns to original instructions
 *  we keeping. it makes it possible to execute the original
 *  instructions and return to the address at the end of the original
 *  instructions. Thus, the execution will goes on.
 *
 */

/*
 * Patch the instruction to the address as given for arguments.
 */
static void patch_code(struct mcount_dynamic_info *mdi, struct mcount_disasm_info *info)
{
	void *origin_code_addr;
	unsigned char call_insn[] = { 0xe8, 0x00, 0x00, 0x00, 0x00 };
	uint32_t target_addr = get_target_addr(mdi, info->addr);

	/* patch address */
	origin_code_addr = (void *)info->addr;

	if (info->has_intel_cet) {
		origin_code_addr += ENDBR_INSN_SIZE;
		target_addr = get_target_addr(mdi, info->addr + ENDBR_INSN_SIZE);
	}

	/* build the instrumentation instruction */
	memcpy(&call_insn[1], &target_addr, CALL_INSN_SIZE - 1);

	/*
	 * we need 5-bytes at least to instrumentation. however,
	 * if instructions is not fit 5-bytes, we will overwrite the
	 * 5-bytes and fill the remaining part of the last
	 * instruction with nop.
	 *
	 * [example]
	 * In this example, we overwrite 9-bytes to use 5-bytes.
	 *
	 * dynamic: 0x19e98b0[01]:push rbp
	 * dynamic: 0x19e98b1[03]:mov rbp, rsp
	 * dynamic: 0x19e98b4[05]:mov edi, 0x4005f4
	 *
	 * dynamic: 0x40054c[05]:call 0x400ff0
	 * dynamic: 0x400551[01]:nop
	 * dynamic: 0x400552[01]:nop
	 * dynamic: 0x400553[01]:nop
	 * dynamic: 0x400554[01]:nop
	 */
	memcpy(origin_code_addr, call_insn, CALL_INSN_SIZE);
	memset(origin_code_addr + CALL_INSN_SIZE, 0x90, /* NOP */
	       info->orig_size - CALL_INSN_SIZE);

	/* flush icache so that cpu can execute the new insn */
	__builtin___clear_cache(origin_code_addr, origin_code_addr + info->orig_size);
}

static int patch_normal_func(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym,
			     struct mcount_disasm_engine *disasm)
{
	uint8_t jmp_insn[15] = {
		0x3e,
		0xff,
		0x25,
	};
	uint64_t jmp_target;
	struct mcount_disasm_info info = {
		.sym = sym,
		.addr = mdi->map->start + sym->addr,
	};
	unsigned call_offset = CALL_INSN_SIZE;
	int state;

	state = disasm_check_insns(disasm, mdi, &info);
	if (state != INSTRUMENT_SUCCESS) {
		pr_dbg3("  >> %s: %s\n", state == INSTRUMENT_FAILED ? "FAIL" : "SKIP", sym->name);
		return state;
	}

	pr_dbg2("force patch normal func: %s (patch size: %d)\n", sym->name, info.orig_size);

	/*
	 *  stored origin instruction block:
	 *  ----------------------
	 *  | [origin_code_size] |
	 *  ----------------------
	 *  | [jmpq    *0x0(rip) |
	 *  ----------------------
	 *  | [Return   address] |
	 *  ----------------------
	 */
	jmp_target = info.addr + info.orig_size;
	if (info.has_intel_cet) {
		jmp_target += ENDBR_INSN_SIZE;
		call_offset += ENDBR_INSN_SIZE;
	}

	memcpy(jmp_insn + CET_JMP_INSN_SIZE, &jmp_target, sizeof(jmp_target));

	if (info.has_jump)
		mcount_save_code(&info, call_offset, jmp_insn, 0);
	else
		mcount_save_code(&info, call_offset, jmp_insn, sizeof(jmp_insn));

	patch_code(mdi, &info);

	return INSTRUMENT_SUCCESS;
}

static int unpatch_func(uint8_t *insn, char *name)
{
	uint8_t nop5[] = { 0x0f, 0x1f, 0x44, 0x00, 0x00 };
	uint8_t nop6[] = { 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00 };
	uint8_t *nop_insn;
	size_t nop_size;

	if (*insn == 0xe8) {
		nop_insn = nop5;
		nop_size = sizeof(nop5);
	}
	else if (insn[0] == 0xff && insn[1] == 0x15) {
		nop_insn = nop6;
		nop_size = sizeof(nop6);
	}
	else {
		return INSTRUMENT_SKIPPED;
	}

	pr_dbg3("unpatch fentry: %s\n", name);
	memcpy(insn, nop_insn, nop_size);
	__builtin___clear_cache((void *)insn, (void *)insn + nop_size);

	return INSTRUMENT_SUCCESS;
}

static int unpatch_fentry_func(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym)
{
	uint64_t sym_addr = sym->addr + mdi->map->start;

	return unpatch_func((void *)sym_addr, sym->name);
}

static int cmp_loc(const void *a, const void *b)
{
	const struct motrace_symbol *sym = a;
	uintptr_t loc = *(uintptr_t *)b;

	if (sym->addr <= loc && loc < sym->addr + sym->size)
		return 0;

	return sym->addr > loc ? 1 : -1;
}

static int unpatch_mcount_func(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym)
{
	unsigned long *mcount_loc = mdi->patch_target;
	uintptr_t *loc;

	if (mdi->nr_patch_target != 0) {
		loc = bsearch(sym, mcount_loc, mdi->nr_patch_target, sizeof(*mcount_loc), cmp_loc);

		if (loc != NULL) {
			uint8_t *insn = (uint8_t *)*loc;
			return unpatch_func(insn + mdi->map->start, sym->name);
		}
	}

	return INSTRUMENT_SKIPPED;
}

int mcount_patch_func(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym,
		      struct mcount_disasm_engine *disasm, unsigned min_size)
{
	int result = INSTRUMENT_SKIPPED;

	if (min_size < CALL_INSN_SIZE + 1)
		min_size = CALL_INSN_SIZE + 1;

	if (sym->size < min_size)
		return result;

	switch (mdi->type) {
	case DYNAMIC_XRAY:
		//  reuse xray to patch mo_entry and mo_exit
		result = patch_mo_func(mdi, sym);
		break;

	case DYNAMIC_FENTRY_NOP:
		result = patch_fentry_func(mdi, sym);
		break;

	case DYNAMIC_PATCHABLE:
		result = patch_patchable_func(mdi, sym);
		break;

	case DYNAMIC_NONE:
		result = patch_normal_func(mdi, sym, disasm);
		break;

	default:
		break;
	}
	return result;
}

int mcount_unpatch_func(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym,
			struct mcount_disasm_engine *disasm)
{
	int result = INSTRUMENT_SKIPPED;

	switch (mdi->type) {
	case DYNAMIC_FENTRY:
	case DYNAMIC_PATCHABLE:
		result = unpatch_fentry_func(mdi, sym);
		break;

	case DYNAMIC_PG:
		result = unpatch_mcount_func(mdi, sym);
		break;

	default:
		break;
	}
	return result;
}

static void revert_normal_func(struct mcount_dynamic_info *mdi, struct motrace_symbol *sym,
			       struct mcount_disasm_engine *disasm)
{
	void *addr = (void *)(uintptr_t)sym->addr + mdi->map->start;
	struct mcount_orig_insn *moi;

	if (!memcmp(addr, endbr64, sizeof(endbr64)))
		addr += sizeof(endbr64);

	moi = mcount_find_insn((uintptr_t)addr + CALL_INSN_SIZE);
	if (moi == NULL)
		return;

	memcpy(addr, moi->orig, moi->orig_size);
	__builtin___clear_cache(addr, addr + moi->orig_size);
}

void mcount_arch_dynamic_recover(struct mcount_dynamic_info *mdi,
				 struct mcount_disasm_engine *disasm)
{
	struct dynamic_bad_symbol *badsym, *tmp;

	list_for_each_entry_safe(badsym, tmp, &mdi->bad_syms, list) {
		if (!badsym->reverted)
			revert_normal_func(mdi, badsym->sym, disasm);

		list_del(&badsym->list);
		free(badsym);
	}
}

static bool addr_in_prologue(struct mcount_disasm_info *info, unsigned long addr)
{
	return info->addr <= addr && addr < (info->addr + info->orig_size);
}

int mcount_arch_branch_table_size(struct mcount_disasm_info *info)
{
	struct cond_branch_info *jcc_info;
	int count = 0;
	int i;

	for (i = 0; i < info->nr_branch; i++) {
		jcc_info = &info->branch_info[i];

		/* no need to allocate entry for jcc that jump directly to prologue */
		if (addr_in_prologue(info, jcc_info->branch_target))
			continue;

		count++;
	}
	return count * ARCH_BRANCH_ENTRY_SIZE;
}

void mcount_arch_patch_branch(struct mcount_disasm_info *info, struct mcount_orig_insn *orig)
{
	/*
	 * The first entry in the table starts right after the out-of-line
	 * execution buffer.
	 */
	uint64_t entry_offset = orig->insn_size;
	uint8_t trampoline[ARCH_TRAMPOLINE_SIZE] = {
		0x3e,
		0xff,
		0x25,
	};
	struct cond_branch_info *jcc_info;
	unsigned long jcc_target;
	unsigned long jcc_index;
	uint32_t disp;
	int i;

	for (i = 0; i < info->nr_branch; i++) {
		jcc_info = &info->branch_info[i];
		jcc_target = jcc_info->branch_target;
		jcc_index = jcc_info->insn_index;

		/* leave the original disp of jcc that target the prologue as it is */
		if (addr_in_prologue(info, jcc_target)) {
			jcc_target -= jcc_info->insn_addr + jcc_info->insn_size;
			info->insns[jcc_index + 1] = jcc_target;
			continue;
		}

		/* setup the branch entry trampoline */
		memcpy(trampoline + CET_JMP_INSN_SIZE, &jcc_target, sizeof(jcc_target));

		/* write the entry to the branch table */
		memcpy(orig->insn + entry_offset, trampoline, sizeof(trampoline));

		/* previously, all jcc32 are downgraded to jcc8 */
		disp = entry_offset - (jcc_index + JCC8_INSN_SIZE);
		if (disp > SCHAR_MAX) { /* should not happen */
			pr_err("target is not in reach");
		}

		/* patch jcc displacement to target corresponding entry in the table */
		info->insns[jcc_index + 1] = disp;

		entry_offset += ARCH_BRANCH_ENTRY_SIZE;
	}
}
