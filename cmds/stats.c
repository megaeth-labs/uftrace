/*
 * motrace stats command related routines
 *
 * Copyright (C) 2014-2018, LG Electronics
 *
 * Released under the GPL v2.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "motrace.h"
#include "utils/fstack.h"
#include "utils/rbtree.h"
#include "utils/symbol.h"
#include "utils/utils.h"

struct time_vec {
	uint64_t *values;
	size_t nr;
	size_t cap;
};

struct tid_stats {
	struct rb_node rb;
	int tid;
	struct time_vec total_times;
	struct time_vec offcpu_times;
};

struct func_stats {
	struct rb_node rb;
	char *name;
	struct rb_root tids;
};

static void time_vec_add(struct time_vec *vec, uint64_t value)
{
	if (vec->nr == vec->cap) {
		size_t new_cap = vec->cap ? vec->cap * 2 : 64;

		vec->values = xrealloc(vec->values, new_cap * sizeof(*vec->values));
		vec->cap = new_cap;
	}
	vec->values[vec->nr++] = value;
}

static struct func_stats *find_or_create_func(struct rb_root *root, const char *name)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;

	while (*p) {
		struct func_stats *iter = rb_entry(*p, struct func_stats, rb);
		int cmp = strcmp(name, iter->name);

		parent = *p;
		if (cmp < 0)
			p = &(*p)->rb_left;
		else if (cmp > 0)
			p = &(*p)->rb_right;
		else
			return iter;
	}

	struct func_stats *node = xzalloc(sizeof(*node));
	node->name = xstrdup(name);
	node->tids = RB_ROOT;

	rb_link_node(&node->rb, parent, p);
	rb_insert_color(&node->rb, root);
	return node;
}

static struct tid_stats *find_or_create_tid(struct rb_root *root, int tid)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;

	while (*p) {
		struct tid_stats *iter = rb_entry(*p, struct tid_stats, rb);

		parent = *p;
		if (tid < iter->tid)
			p = &(*p)->rb_left;
		else if (tid > iter->tid)
			p = &(*p)->rb_right;
		else
			return iter;
	}

	struct tid_stats *node = xzalloc(sizeof(*node));
	node->tid = tid;

	rb_link_node(&node->rb, parent, p);
	rb_insert_color(&node->rb, root);
	return node;
}

static void add_sample(struct rb_root *root, int tid, const char *name, uint64_t duration,
		       uint64_t offcpu_duration, bool has_offcpu)
{
	struct func_stats *func = find_or_create_func(root, name);
	struct tid_stats *tstat = find_or_create_tid(&func->tids, tid);

	time_vec_add(&tstat->total_times, duration);
	if (has_offcpu)
		time_vec_add(&tstat->offcpu_times, offcpu_duration);
}

static int cmp_u64(const void *a, const void *b)
{
	const uint64_t va = *(const uint64_t *)a;
	const uint64_t vb = *(const uint64_t *)b;

	if (va == vb)
		return 0;
	return va < vb ? -1 : 1;
}

static uint64_t percentile_value(uint64_t *values, size_t nr, unsigned pct)
{
	size_t rank;

	if (nr == 0)
		return 0;

	rank = (pct * nr + 99) / 100;
	if (rank == 0)
		rank = 1;
	if (rank > nr)
		rank = nr;

	return values[rank - 1];
}

static void add_sample_by_addr(struct rb_root *root, struct motrace_task_reader *task,
			       uint64_t timestamp, uint64_t addr, uint64_t duration,
			       uint64_t offcpu_duration, bool has_offcpu, struct motrace_opts *opts)
{
	struct motrace_symbol *sym;
	char *symname;

	if (is_sched_event(addr))
		return;

	sym = task_find_sym_addr(&task->h->sessions, task, timestamp, addr);
	if (!opts->libcall && sym && sym->type == ST_PLT_FUNC)
		return;

	symname = symbol_getname(sym, addr);
	add_sample(root, task->tid, symname, duration, offcpu_duration, has_offcpu);
	symbol_putname(sym, symname);
}

static void add_lost_fstack(struct rb_root *root, struct motrace_task_reader *task,
			    struct motrace_opts *opts)
{
	struct motrace_fstack *fstack;
	struct motrace_sym_info *sinfo = &task->h->sessions.first->sym_info;

	while (task->stack_count >= task->user_stack_count) {
		fstack = fstack_get(task, task->stack_count);

		if (fstack_enabled && fstack && fstack->valid &&
		    !(fstack->flags & FSTACK_FL_NORECORD)) {
			bool has_offcpu = false;
			uint64_t offcpu_total = 0;

			if ((task->h->hdr.feat_mask & OFFCPU) &&
			    !is_kernel_address(sinfo, fstack->addr)) {
				if (fstack->total_time > fstack->cpu_time)
					offcpu_total = fstack->total_time - fstack->cpu_time;
				has_offcpu = true;
			}

			add_sample_by_addr(root, task, task->timestamp_last, fstack->addr,
					   fstack->total_time, offcpu_total, has_offcpu, opts);
		}

		fstack_exit(task);
		task->stack_count--;
	}
}

static void add_remaining_fstack(struct motrace_data *handle, struct rb_root *root,
				 struct motrace_opts *opts)
{
	struct motrace_task_reader *task;
	struct motrace_fstack *fstack;
	struct motrace_sym_info *sinfo = &handle->sessions.first->sym_info;
	int i;

	for (i = 0; i < handle->nr_tasks; i++) {
		uint64_t last_time;

		task = &handle->tasks[i];

		if (task->stack_count == 0)
			continue;

		last_time = task->rstack->time;

		if (handle->time_range.stop)
			last_time = handle->time_range.stop;

		while (--task->stack_count >= 0) {
			fstack = fstack_get(task, task->stack_count);
			if (fstack == NULL)
				continue;

			if (fstack->total_time > last_time)
				continue;

			fstack->total_time = last_time - fstack->total_time;
			if (fstack->child_time > fstack->total_time)
				fstack->total_time = fstack->child_time;

			if (task->stack_count > 0)
				fstack[-1].child_time += fstack->total_time;

			{
				bool has_offcpu = false;
				uint64_t offcpu_total = 0;

				if ((handle->hdr.feat_mask & OFFCPU) &&
				    !is_kernel_address(sinfo, fstack->addr)) {
					if (fstack->total_time > fstack->cpu_time)
						offcpu_total = fstack->total_time - fstack->cpu_time;
					has_offcpu = true;
				}

				add_sample_by_addr(root, task, last_time, fstack->addr,
						   fstack->total_time, offcpu_total, has_offcpu,
						   opts);
			}
		}
	}
}

static void build_stats(struct motrace_data *handle, struct motrace_opts *opts,
			struct rb_root *root)
{
	struct motrace_session_link *sessions = &handle->sessions;
	struct motrace_record *rstack;
	struct motrace_task_reader *task;
	struct motrace_fstack *fstack;
	struct motrace_symbol *sym;
	uint64_t addr;

	while (read_rstack(handle, &task) >= 0 && !motrace_done) {
		rstack = task->rstack;

		if (rstack->type != MOTRACE_LOST)
			task->timestamp_last = rstack->time;

		if (!fstack_check_opts(task, opts))
			continue;

		if (!fstack_check_filter(task))
			continue;

		if (rstack->type == MOTRACE_ENTRY) {
			fstack_check_filter_done(task);
			continue;
		}

		if (rstack->type == MOTRACE_EVENT)
			continue;

		if (rstack->type == MOTRACE_LOST) {
			add_lost_fstack(root, task, opts);
			continue;
		}

		addr = rstack->addr;
		if (is_kernel_record(task, rstack)) {
			struct motrace_session *fsess;

			fsess = sessions->first;
			addr = get_kernel_address(&fsess->sym_info, rstack->addr);
		}

		sym = task_find_sym(sessions, task, rstack);
		if (!opts->libcall && sym && sym->type == ST_PLT_FUNC) {
			fstack_check_filter_done(task);
			continue;
		}

		fstack = fstack_get(task, task->stack_count);
		if (fstack == NULL) {
			fstack_check_filter_done(task);
			continue;
		}

		{
			bool has_offcpu = false;
			uint64_t offcpu_total = 0;

			if ((handle->hdr.feat_mask & OFFCPU) && is_user_record(task, rstack)) {
				if (fstack->total_time > fstack->cpu_time)
					offcpu_total = fstack->total_time - fstack->cpu_time;
				has_offcpu = true;
			}

			add_sample_by_addr(root, task, rstack->time, addr, fstack->total_time,
					   offcpu_total, has_offcpu, opts);
		}

		fstack_check_filter_done(task);
	}

	if (motrace_done)
		return;

	add_remaining_fstack(handle, root, opts);
}

static void print_stats(struct rb_root *root)
{
	struct rb_node *fn;

	pr_out("# motrace stats (per-function per-TID)\n");
	pr_out("# Columns: TID  Calls  Min  P50  P90  P99  Max\n");

	for (fn = rb_first(root); fn != NULL; fn = rb_next(fn)) {
		struct func_stats *func = rb_entry(fn, struct func_stats, rb);
		struct rb_node *tn;
		bool has_offcpu = false;

		pr_out("\n%s\n", func->name);
		pr_out("  total  %5s %7s %11s %11s %11s %11s %11s\n", "TID", "Calls", "Min", "P50",
		       "P90", "P99", "Max");

		for (tn = rb_first(&func->tids); tn != NULL; tn = rb_next(tn)) {
			struct tid_stats *tstat = rb_entry(tn, struct tid_stats, rb);
			uint64_t min;
			uint64_t max;
			uint64_t p50;
			uint64_t p90;
			uint64_t p99;

			if (tstat->total_times.nr == 0)
				continue;

			qsort(tstat->total_times.values, tstat->total_times.nr,
			      sizeof(*tstat->total_times.values), cmp_u64);

			min = tstat->total_times.values[0];
			max = tstat->total_times.values[tstat->total_times.nr - 1];
			p50 = percentile_value(tstat->total_times.values, tstat->total_times.nr,
					       50);
			p90 = percentile_value(tstat->total_times.values, tstat->total_times.nr,
					       90);
			p99 = percentile_value(tstat->total_times.values, tstat->total_times.nr,
					       99);

			pr_out("         %5d %7zu ", tstat->tid, tstat->total_times.nr);
			print_time_unit(min);
			pr_out(" ");
			print_time_unit(p50);
			pr_out(" ");
			print_time_unit(p90);
			pr_out(" ");
			print_time_unit(p99);
			pr_out(" ");
			print_time_unit(max);
			pr_out("\n");
		}

		for (tn = rb_first(&func->tids); tn != NULL; tn = rb_next(tn)) {
			struct tid_stats *tstat = rb_entry(tn, struct tid_stats, rb);

			if (tstat->offcpu_times.nr > 0) {
				has_offcpu = true;
				break;
			}
		}

		if (!has_offcpu)
			continue;

		pr_out("  offcpu %5s %7s %11s %11s %11s %11s %11s\n", "TID", "Calls", "Min",
		       "P50", "P90", "P99", "Max");

		for (tn = rb_first(&func->tids); tn != NULL; tn = rb_next(tn)) {
			struct tid_stats *tstat = rb_entry(tn, struct tid_stats, rb);
			uint64_t min;
			uint64_t max;
			uint64_t p50;
			uint64_t p90;
			uint64_t p99;

			if (tstat->offcpu_times.nr == 0)
				continue;

			qsort(tstat->offcpu_times.values, tstat->offcpu_times.nr,
			      sizeof(*tstat->offcpu_times.values), cmp_u64);

			min = tstat->offcpu_times.values[0];
			max = tstat->offcpu_times.values[tstat->offcpu_times.nr - 1];
			p50 = percentile_value(tstat->offcpu_times.values, tstat->offcpu_times.nr,
					       50);
			p90 = percentile_value(tstat->offcpu_times.values, tstat->offcpu_times.nr,
					       90);
			p99 = percentile_value(tstat->offcpu_times.values, tstat->offcpu_times.nr,
					       99);

			pr_out("         %5d %7zu ", tstat->tid, tstat->offcpu_times.nr);
			print_time_unit(min);
			pr_out(" ");
			print_time_unit(p50);
			pr_out(" ");
			print_time_unit(p90);
			pr_out(" ");
			print_time_unit(p99);
			pr_out(" ");
			print_time_unit(max);
			pr_out("\n");
		}
	}
}

static void free_stats(struct rb_root *root)
{
	while (!RB_EMPTY_ROOT(root)) {
		struct rb_node *fn = rb_first(root);
		struct func_stats *func = rb_entry(fn, struct func_stats, rb);

		while (!RB_EMPTY_ROOT(&func->tids)) {
			struct rb_node *tn = rb_first(&func->tids);
			struct tid_stats *tstat = rb_entry(tn, struct tid_stats, rb);

			rb_erase(tn, &func->tids);
			free(tstat->total_times.values);
			free(tstat->offcpu_times.values);
			free(tstat);
		}

		rb_erase(fn, root);
		free(func->name);
		free(func);
	}
}

int command_stats(int argc, char *argv[], struct motrace_opts *opts)
{
	int ret;
	struct motrace_data handle;
	struct rb_root func_root = RB_ROOT;

	ret = open_data_file(opts, &handle);
	if (ret < 0) {
		pr_warn("cannot open record data: %s: %m\n", opts->dirname);
		return -1;
	}

	fstack_setup_filters(opts, &handle);

	if (format_mode == FORMAT_HTML)
		pr_out(HTML_HEADER);

	build_stats(&handle, opts, &func_root);
	print_stats(&func_root);
	free_stats(&func_root);

	if (format_mode == FORMAT_HTML)
		pr_out(HTML_FOOTER);

	close_data_file(opts, &handle);

	return 0;
}
