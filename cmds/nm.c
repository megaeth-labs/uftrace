/*
 * motrace nm command related routines
 *
 * Copyright (C) 2014-2018, LG Electronics, Namhyung Kim <namhyung.kim@lge.com>
 *
 * Released under the GPL v2.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "motrace.h"
#include "utils/utils.h"
#include "utils/symbol.h"

struct module_list {
	struct motrace_module *mod;
	struct module_list *next;
};

static bool is_module_printed(struct module_list *list, struct motrace_module *mod)
{
	while (list) {
		if (list->mod == mod)
			return true;
		list = list->next;
	}
	return false;
}

static void add_printed_module(struct module_list **list, struct motrace_module *mod)
{
	struct module_list *node = xmalloc(sizeof(*node));
	node->mod = mod;
	node->next = *list;
	*list = node;
}

static void free_module_list(struct module_list *list)
{
	struct module_list *next;
	while (list) {
		next = list->next;
		free(list);
		list = next;
	}
}

static int print_session_modules(struct motrace_session *sess, void *arg)
{
	struct module_list **printed = arg;
	struct motrace_mmap *map;

	for_each_map(&sess->sym_info, map) {
		if (map->mod && !is_module_printed(*printed, map->mod)) {
			printf("Symbols for %s (build-id: %s)\n", map->mod->name, map->mod->build_id);
			print_symtab(&map->mod->symtab);
			printf("\n");
			add_printed_module(printed, map->mod);
		}
	}
	return 0;
}

static int print_binary_symbols(struct motrace_opts *opts)
{
	struct motrace_sym_info sinfo = {
		.dirname = opts->dirname,
		.filename = opts->exename,
		.symdir = opts->with_syms ? opts->with_syms : opts->dirname,
		.flags = SYMTAB_FL_DEMANGLE,
	};
	struct motrace_module *mod;
	char build_id[BUILD_ID_STR_SIZE];

	if (opts->with_syms)
		sinfo.flags |= SYMTAB_FL_USE_SYMFILE | SYMTAB_FL_SYMS_DIR;

	if (read_build_id(opts->exename, build_id, sizeof(build_id)) < 0)
		build_id[0] = '\0';

	mod = load_module_symtab(&sinfo, sinfo.filename, build_id);
	if (mod == NULL)
		return -1;

	printf("Symbols for %s (build-id: %s)\n", mod->name, mod->build_id);
	print_symtab(&mod->symtab);
	printf("\n");

	unload_module_symtabs();
	return 0;
}

static int ensure_output_dir(const char *dir)
{
	struct stat st;

	if (dir == NULL || dir[0] == '\0')
		return -1;

	if (stat(dir, &st) == 0) {
		if (!S_ISDIR(st.st_mode))
			pr_err("not a directory: %s\n", dir);
		return 0;
	}

	if (errno != ENOENT)
		pr_err("cannot access %s: %m\n", dir);

	if (mkdir(dir, 0755) < 0)
		pr_err("cannot create %s: %m\n", dir);

	return 0;
}

static int write_binary_symfile(struct motrace_opts *opts)
{
	struct motrace_sym_info sinfo = {
		.dirname = opts->dirname,
		.filename = opts->exename,
		.symdir = opts->with_syms ? opts->with_syms : opts->dirname,
		.flags = SYMTAB_FL_DEMANGLE | SYMTAB_FL_ADJ_OFFSET,
	};
	struct motrace_module *mod;
	char build_id[BUILD_ID_STR_SIZE];

	if (opts->with_syms)
		sinfo.flags |= SYMTAB_FL_USE_SYMFILE | SYMTAB_FL_SYMS_DIR;

	if (ensure_output_dir(opts->nm_output_dir) < 0)
		return -1;

	if (read_build_id(opts->exename, build_id, sizeof(build_id)) < 0)
		build_id[0] = '\0';

	mod = load_module_symtab(&sinfo, sinfo.filename, build_id);
	if (mod == NULL)
		return -1;

	save_module_symtabs(opts->nm_output_dir);
	unload_module_symtabs();
	return 0;
}

int command_nm(int argc, char *argv[], struct motrace_opts *opts)
{
	struct motrace_session_link link = {
		.root = RB_ROOT,
		.tasks = RB_ROOT,
	};
	struct module_list *printed = NULL;
	struct stat st;

	if (opts->nm_format == NM_FORMAT_SYM) {
		if (opts->exename == NULL || opts->exename[0] == '\0')
			pr_err_ns("nm --format=sym requires a binary path\n");
		if (stat(opts->exename, &st) < 0)
			pr_err("cannot access %s: %m\n", opts->exename);
		if (!S_ISREG(st.st_mode))
			pr_err("nm --format=sym requires a regular file: %s\n", opts->exename);
		if (opts->nm_output_dir == NULL || opts->nm_output_dir[0] == '\0')
			pr_err_ns("nm --format=sym requires --output-dir=DIR\n");
		return write_binary_symfile(opts);
	}

	if (opts->exename) {
		if (stat(opts->exename, &st) < 0) {
			pr_err("cannot access %s: %m\n", opts->exename);
			return -1;
		}

		if (S_ISREG(st.st_mode))
			return print_binary_symbols(opts);

		if (S_ISDIR(st.st_mode))
			opts->dirname = opts->exename;
		else {
			pr_err("unsupported file type: %s\n", opts->exename);
			return -1;
		}
	}

	/*
	 * Load tasks and symbols.
	 * We need symbol tables (needs_symtab=true).
	 * We use relative addresses for modules (sym_rel_addr=true).
	 * We don't need source lines (needs_srcline=false).
	 */
	if (read_task_file(&link, opts->dirname, true, true, false) < 0) {
		/* try text format if binary format failed */
		if (read_task_txt_file(&link, opts->dirname, opts->dirname, true, true, false) < 0) {
			pr_err("failed to read session data from %s\n", opts->dirname);
			return -1;
		}
	}

	walk_sessions(&link, print_session_modules, &printed);

	free_module_list(printed);
	delete_sessions(&link);
	unload_module_symtabs();

	return 0;
}
