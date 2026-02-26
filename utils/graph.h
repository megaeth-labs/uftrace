#ifndef MOTRACE_GRAPH_H
#define MOTRACE_GRAPH_H

#include <stdbool.h>
#include <stdint.h>

#include "motrace.h"
#include "utils/fstack.h"
#include "utils/list.h"
#include "utils/rbtree.h"

struct motrace_graph_node {
	uint64_t addr;
	char *name;
	int nr_edges;
	int nr_calls;
	uint64_t time;
	uint64_t child_time;
	uint64_t cpu_time;
	uint64_t child_cpu_time;
	uint32_t id;
	struct list_head head;
	struct list_head list;
	struct motrace_graph_node *parent;
	struct motrace_dbg_loc *loc;
};

enum motrace_graph_node_type {
	NODE_T_NORMAL,
	NODE_T_FORK,
	NODE_T_EXEC,
};

struct motrace_special_node {
	struct list_head list;
	struct motrace_graph_node *node;
	enum motrace_graph_node_type type;
	int pid;
};

struct motrace_graph {
	bool kernel_only;
	struct motrace_session *sess;
	struct list_head special_nodes;
	struct motrace_graph_node root;
};

struct motrace_task_graph {
	bool lost;
	bool new_sess;
	struct motrace_task_reader *task;
	struct motrace_graph *graph;
	struct motrace_graph_node *node;
	struct rb_node link;
};

typedef void (*graph_fn)(struct motrace_task_graph *tg, void *arg);

void graph_init(struct motrace_graph *graph, struct motrace_session *s);
void graph_init_callbacks(graph_fn entry, graph_fn exit, graph_fn event, void *arg);
void graph_destroy(struct motrace_graph *graph);

struct motrace_task_graph *graph_get_task(struct motrace_task_reader *task, size_t tg_size);
void graph_remove_task(void);

int graph_add_node(struct motrace_task_graph *tg, int type, char *name, size_t node_size,
		   struct motrace_dbg_loc *loc);
struct motrace_graph_node *graph_find_node(struct motrace_graph_node *parent, uint64_t addr);

#endif /* MOTRACE_GRAPH_H */
