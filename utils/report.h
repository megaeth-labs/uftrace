#ifndef MOTRACE_REPORT_H
#define MOTRACE_REPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "motrace.h"
#include "utils/rbtree.h"

enum avg_mode {
	AVG_NONE,
	AVG_TOTAL,
	AVG_SELF,
	AVG_ANY,
};

struct report_time_stat {
	uint64_t sum;
	uint64_t rec; /* time in recursive call */
	uint64_t sum_sq;
	uint64_t rec_sq;
	uint64_t avg;
	double stdv;
	uint64_t min;
	uint64_t max;
};

struct motrace_report_node {
	char *name;
	struct report_time_stat total;
	struct report_time_stat self;
	struct report_time_stat offcpu_total;
	struct report_time_stat offcpu_self;
	struct motrace_dbg_loc *loc;
	uint64_t call;
	struct rb_node name_link;
	struct rb_node sort_link;
	unsigned size;

	/* used by diff */
	struct motrace_report_node *pair;
};

struct motrace_diff_policy {
	/* show percentage rather than value of diff */
	bool percent;

	/* calculate diff using absolute values */
	bool absolute;

	/* show original data as well as difference */
	bool full;
};

extern struct motrace_diff_policy diff_policy;

struct motrace_report_node *report_find_node(struct rb_root *root, const char *name);
void report_add_node(struct rb_root *root, const char *name, struct motrace_report_node *node);
void report_update_node(struct motrace_report_node *node, struct motrace_task_reader *task,
			struct motrace_dbg_loc *loc);
void report_calc_avg(struct rb_root *root);
void report_delete_node(struct rb_root *root, struct motrace_report_node *node);

char *convert_sort_keys(char *sort_keys, enum avg_mode avg_mode);
int report_setup_sort(const char *sort_keys);
void report_sort_nodes(struct rb_root *name_root, struct rb_root *sort_root);

int report_setup_diff(const char *key_str);
void report_diff_nodes(struct rb_root *orig_root, struct rb_root *pair_root,
		       struct rb_root *diff_root, int diff_column);
void destroy_diff_nodes(struct rb_root *orig_root, struct rb_root *pair_root);
void apply_diff_policy(char *policy);

int report_setup_task(const char *key_str);
void report_sort_tasks(struct motrace_data *handle, struct rb_root *name_root,
		       struct rb_root *sort_root);

void setup_report_field(struct list_head *output_fields, struct motrace_opts *opts,
			enum avg_mode avg_mode);

#endif /* MOTRACE_REPORT_H */
