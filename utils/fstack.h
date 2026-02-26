#ifndef MOTRACE_FSTACK_H
#define MOTRACE_FSTACK_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "motrace.h"
#include "utils/filter.h"

struct motrace_symbol;

enum motrace_fstack_flag {
	FSTACK_FL_FILTERED = (1U << 0),
	FSTACK_FL_NOTRACE = (1U << 1),
	FSTACK_FL_NORECORD = (1U << 2),
	FSTACK_FL_EXEC = (1U << 3),
	FSTACK_FL_LONGJMP = (1U << 4),
};

enum motrace_fstack_context {
	FSTACK_CTX_UNKNOWN = 0,
	FSTACK_CTX_USER = 1,
	FSTACK_CTX_KERNEL = 2,
};

struct motrace_task_filter_stack {
	struct motrace_task_filter_stack *next;
	uint64_t threshold;
	int depth;
	unsigned size;
	enum motrace_fstack_context context;
};

struct motrace_task_reader {
	int tid;
	bool valid;
	bool done;
	bool lost_seen;
	bool sched_out_seen;
	bool fork_handled;
	bool fstack_set;
	bool display_depth_set;
	bool fstack_warned;
	FILE *fp;
	struct motrace_symbol *func;
	struct motrace_task *t;
	struct motrace_data *h;
	struct motrace_record ustack;
	struct motrace_record kstack;
	struct motrace_record estack;
	struct motrace_record xstack;
	struct motrace_record *rstack;
	struct motrace_rstack_list rstack_list;
	struct motrace_rstack_list event_list;
	int stack_count;
	int lost_count;
	int user_stack_count;
	int display_depth;
	int user_display_depth;
	int fork_display_depth;
	int column_index;
	int event_color;
	int sched_cpu;
	enum motrace_fstack_context ctx;
	uint64_t timestamp;
	uint64_t timestamp_last;
	uint64_t timestamp_next;
	uint64_t timestamp_estimate;
	struct {
		int in_count;
		int out_count;
		int depth;
		struct motrace_task_filter_stack *stack;
	} filter;
	struct motrace_fstack {
		uint64_t addr;
		bool valid;
		int orig_depth;
		unsigned long flags;
		uint64_t total_time;
		uint64_t child_time;
		uint64_t cpu_time;
		uint64_t child_cpu_time;
	} * func_stack;
	struct motrace_fstack_args args;
	bool sched_preempt_seen;
};

enum motrace_argspec_string_bits {
	/* bit index */
	NEEDS_PAREN_BIT,
	NEEDS_SEMI_COLON_BIT,
	HAS_MORE_BIT,
	IS_RETVAL_BIT,
	NEEDS_ASSIGNMENT_BIT,
	NEEDS_JSON_BIT,

	/* bit mask */
	NEEDS_PAREN = (1U << NEEDS_PAREN_BIT),
	NEEDS_SEMI_COLON = (1U << NEEDS_SEMI_COLON_BIT),
	HAS_MORE = (1U << HAS_MORE_BIT),
	IS_RETVAL = (1U << IS_RETVAL_BIT),
	NEEDS_ASSIGNMENT = (1U << NEEDS_ASSIGNMENT_BIT),
	NEEDS_JSON = (1U << NEEDS_JSON_BIT),
};

extern bool fstack_enabled;
extern bool live_disabled;

struct motrace_task_reader *get_task_handle(struct motrace_data *handle, int tid);
void reset_task_handle(struct motrace_data *handle);

void fstack_setup_task(char *tid_filter, struct motrace_data *handle);

int read_rstack(struct motrace_data *handle, struct motrace_task_reader **task);
int peek_rstack(struct motrace_data *handle, struct motrace_task_reader **task);
void fstack_consume(struct motrace_data *handle, struct motrace_task_reader *task);

int read_task_ustack(struct motrace_data *handle, struct motrace_task_reader *task);
int read_task_args(struct motrace_task_reader *task, struct motrace_record *rstack, bool is_retval);

static inline bool is_user_record(struct motrace_task_reader *task, struct motrace_record *rec)
{
	return rec == &task->ustack;
}

static inline bool is_kernel_record(struct motrace_task_reader *task, struct motrace_record *rec)
{
	return rec == &task->kstack;
}

static inline bool is_event_record(struct motrace_task_reader *task, struct motrace_record *rec)
{
	return rec == &task->estack;
}

static inline bool is_extern_record(struct motrace_task_reader *task, struct motrace_record *rec)
{
	return rec == &task->xstack;
}

void setup_fstack_args(char *argspec, char *retspec, struct motrace_data *handle,
		       struct motrace_filter_setting *setting);
int fstack_setup_filters(struct motrace_opts *opts, struct motrace_data *handle);

struct motrace_fstack *fstack_get(struct motrace_task_reader *task, int idx);
int fstack_entry(struct motrace_task_reader *task, struct motrace_record *rstack,
		 struct motrace_trigger *tr);
void fstack_exit(struct motrace_task_reader *task);
int fstack_update(int type, struct motrace_task_reader *task, struct motrace_fstack *fstack);
struct motrace_task_reader *fstack_skip(struct motrace_data *handle,
					struct motrace_task_reader *task, int curr_depth,
					struct motrace_opts *opts);
bool fstack_check_filter(struct motrace_task_reader *task);
bool fstack_check_opts(struct motrace_task_reader *task, struct motrace_opts *opts);
void fstack_check_filter_done(struct motrace_task_reader *task);

bool is_sched_event(uint64_t addr);
bool is_sched_preempt_event(struct motrace_task_reader *task, uint64_t addr);

void get_argspec_string(struct motrace_task_reader *task, char *args, size_t len,
			enum motrace_argspec_string_bits str_mode);

#define EXTERN_DATA_MAX 1024

struct motrace_extern_reader {
	FILE *fp;
	bool valid;
	uint64_t time;
	char msg[EXTERN_DATA_MAX];
	struct motrace_record rec;
};

int setup_extern_data(struct motrace_data *handle, struct motrace_opts *opts);
int read_extern_data(struct motrace_data *handle, struct motrace_extern_reader *extn);
struct motrace_record *get_extern_record(struct motrace_extern_reader *extn,
					 struct motrace_record *rec);
int finish_extern_data(struct motrace_data *handle);

static inline bool has_extern_data(struct motrace_data *handle)
{
	return handle->extn != NULL;
}

#endif /* MOTRACE_FSTACK_H */
