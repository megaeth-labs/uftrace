#ifndef MOTRACE_H
#define MOTRACE_H

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "utils/arch.h"
#include "utils/filter.h"
#include "utils/list.h"
#include "utils/perf.h"
#include "utils/rbtree.h"
#include "utils/symbol.h"

#define MOTRACE_MAGIC_LEN 8
#define MOTRACE_MAGIC_STR "Ftrace!"
#define MOTRACE_FILE_VERSION 4
#define MOTRACE_FILE_VERSION_MIN 3
#define MOTRACE_DIR_NAME "motrace.data"
#define MOTRACE_DIR_OLD_NAME "ftrace.dir"

#define MOTRACE_RECV_PORT 8090

/* default option values */
#define OPT_RSTACK_MAX 65535
#define OPT_RSTACK_DEFAULT 1024
#define OPT_DEPTH_MAX OPT_RSTACK_MAX
#define OPT_THRESHOLD_MAX 0xFFFFFFFFFFFFFFFF /* max uint64 = 2^64-1 */
#define OPT_DEPTH_DEFAULT OPT_RSTACK_DEFAULT
#define OPT_COLUMN_OFFSET 8
#define OPT_SORT_COLUMN 2
#define OPT_SORT_KEYS "total"

struct motrace_file_header {
	char magic[MOTRACE_MAGIC_LEN];
	uint32_t version;
	uint16_t header_size;
	uint8_t endian;
	uint8_t elf_class;
	uint64_t feat_mask;
	uint64_t info_mask;
	uint16_t max_stack;
	uint16_t unused1;
	uint32_t unused2;
};

enum motrace_feat_bits {
	/* bit index */
	PLTHOOK_BIT,
	TASK_SESSION_BIT,
	KERNEL_BIT,
	ARGUMENT_BIT,
	RETVAL_BIT,
	SYM_REL_ADDR_BIT,
	MAX_STACK_BIT,
	EVENT_BIT,
	PERF_EVENT_BIT,
	AUTO_ARGS_BIT,
	DEBUG_INFO_BIT,
	ESTIMATE_RETURN_BIT,
	SYM_SIZE_BIT,
	OFFCPU_BIT,

	FEAT_BIT_MAX,

	/* bit mask */
	PLTHOOK = (1U << PLTHOOK_BIT),
	TASK_SESSION = (1U << TASK_SESSION_BIT),
	KERNEL = (1U << KERNEL_BIT),
	ARGUMENT = (1U << ARGUMENT_BIT),
	RETVAL = (1U << RETVAL_BIT),
	SYM_REL_ADDR = (1U << SYM_REL_ADDR_BIT),
	MAX_STACK = (1U << MAX_STACK_BIT),
	EVENT = (1U << EVENT_BIT),
	PERF_EVENT = (1U << PERF_EVENT_BIT),
	AUTO_ARGS = (1U << AUTO_ARGS_BIT),
	DEBUG_INFO = (1U << DEBUG_INFO_BIT),
	ESTIMATE_RETURN = (1U << ESTIMATE_RETURN_BIT),
	SYM_SIZE = (1U << SYM_SIZE_BIT),
	OFFCPU = (1U << OFFCPU_BIT),
};

enum motrace_info_bits {
	/* bit index */
	EXE_NAME_BIT,
	EXE_BUILD_ID_BIT,
	EXIT_STATUS_BIT,
	CMDLINE_BIT,
	CPUINFO_BIT,
	MEMINFO_BIT,
	OSINFO_BIT,
	TASKINFO_BIT,
	USAGEINFO_BIT,
	LOADINFO_BIT,
	ARG_SPEC_BIT,
	RECORD_DATE_BIT,
	PATTERN_TYPE_BIT,
	VERSION_BIT,
	UTC_OFFSET_BIT,

	INFO_BIT_MAX,

	/* bit mask */
	EXE_NAME = (1U << EXE_NAME_BIT),
	EXE_BUILD_ID = (1U << EXE_BUILD_ID_BIT),
	EXIT_STATUS = (1U << EXIT_STATUS_BIT),
	CMDLINE = (1U << CMDLINE_BIT),
	CPUINFO = (1U << CPUINFO_BIT),
	MEMINFO = (1U << MEMINFO_BIT),
	OSINFO = (1U << OSINFO_BIT),
	TASKINFO = (1U << TASKINFO_BIT),
	USAGEINFO = (1U << USAGEINFO_BIT),
	LOADINFO = (1U << LOADINFO_BIT),
	ARG_SPEC = (1U << ARG_SPEC_BIT),
	RECORD_DATE = (1U << RECORD_DATE_BIT),
	PATTERN_TYPE = (1U << PATTERN_TYPE_BIT),
	VERSION = (1U << VERSION_BIT),
	UTC_OFFSET = (1U << UTC_OFFSET_BIT),
};

struct motrace_info {
	char *exename;
	unsigned char build_id[20];
	int exit_status;
	char *cmdline;
	int nr_cpus_online;
	int nr_cpus_possible;
	char *cpudesc;
	char *meminfo;
	char *kernel;
	char *hostname;
	char *distro;
	char *argspec;
	char *retspec;
	char *autoarg;
	char *autoret;
	char *autoenum;
	bool auto_args_enabled;
	int nr_tid;
	int *tids;
	double stime;
	double utime;
	char *record_date;
	char *elapsed_time;
	char *utc_offset;
	long vctxsw;
	long ictxsw;
	long maxrss;
	long major_fault;
	long minor_fault;
	long rblock;
	long wblock;
	float load1;
	float load5;
	float load15;
	enum motrace_pattern_type patt_type;
	char *motrace_version;
};

enum {
	MOTRACE_EXIT_SUCCESS = 0,
	MOTRACE_EXIT_FAILURE,
	MOTRACE_EXIT_SIGNALED,
	MOTRACE_EXIT_UNKNOWN,
	MOTRACE_EXIT_FINISHED = 1 << 16,
};

struct kbuffer;
struct pevent;
struct motrace_record;
struct motrace_rstack_list;
struct motrace_session;
struct motrace_kernel_reader;
struct motrace_perf_reader;
struct motrace_extern_reader;
struct motrace_module;

struct motrace_session_link {
	struct rb_root root;
	struct rb_root tasks;
	struct motrace_session *first;
	struct motrace_task *first_task;
};

struct motrace_data {
	FILE *fp;
	int sock;
	const char *dirname;
	enum motrace_cpu_arch arch;
	struct motrace_file_header hdr;
	struct motrace_info info;
	struct motrace_kernel_reader *kernel;
	struct motrace_perf_reader *perf;
	struct motrace_extern_reader *extn;
	struct motrace_task_reader *tasks;
	struct motrace_session_link sessions;
	int nr_tasks;
	int nr_perf;
	int last_perf_idx;
	int depth;
	bool needs_byte_swap;
	bool needs_bit_swap;
	bool perf_event_processed;
	bool caller_filter;
	uint64_t time_filter;
	unsigned size_filter;
	struct motrace_time_range time_range;
	struct list_head events;
};

bool data_is_lp64(struct motrace_data *handle);

#define MOTRACE_MODE_INVALID 0
#define MOTRACE_MODE_ATTACH 1
#define MOTRACE_MODE_REPORT 2
#define MOTRACE_MODE_INFO 3
#define MOTRACE_MODE_RECORD 4
#define MOTRACE_MODE_GRAPH 5
#define MOTRACE_MODE_TUI 6
#define MOTRACE_MODE_NM 7
#define MOTRACE_MODE_STATS 8

#define MOTRACE_MODE_DEFAULT MOTRACE_MODE_ATTACH

enum motrace_nm_format {
	NM_FORMAT_TEXT,
	NM_FORMAT_SYM,
};

struct motrace_opts {
	char *lib_path;
	char *filter;
	char *trigger;
	char *sig_trigger;
	char *tid;
	char *exename;
	char *dirname;
	char *logfile;
	char *host;
	char *sort_keys;
	char *args;
	char *retval;
	char *diff;
	char *fields;
	char *patch;
	char *event;
	char *watch;
	char **run_cmd;
	char *opt_file;
	char *diff_policy;
	char *caller;
	char *extern_data;
	char *hide;
	char *loc_filter;
	char *with_syms;
	char *nm_output_dir;
	char *clock;
	int mode;
	int idx;
	int depth;
	int kernel_depth;
	int max_stack;
	int port;
	int color;
	int column_offset;
	int sort_column;
	int nr_thread;
	int rt_prio;
	int size_filter;
	int pid;
	unsigned long bufsize;
	unsigned long kernel_bufsize;
	uint64_t threshold;
	uint64_t sample_time;
	bool flat;
	bool libcall;
	bool print_symtab;
	bool force;
	bool show_task;
	bool no_merge;
	bool nop;
	bool time;
	bool backtrace;
	bool use_pager;
	bool avg_total;
	bool avg_self;
	bool report;
	bool column_view;
	bool want_bind_not;
	bool task_newline;
	bool comment;
	bool libmcount_single;
	bool kernel;
	bool kernel_skip_out; /* also affects VDSO filter */
	bool kernel_only;
	bool keep_pid;
	bool list_event;
	bool event_skip_out;
	bool no_event;
	bool no_sched;
	bool no_sched_preempt;
	bool nest_libcall;
	bool record;
	bool auto_args;
	bool show_args;
	bool libname;
	bool no_randomize_addr;
	bool srcline;
	bool estimate_return;
	bool offcpu;
	bool agent;
	enum motrace_nm_format nm_format;
	struct motrace_time_range range;
	enum motrace_pattern_type patt_type;
	enum motrace_trace_state trace;
};

extern struct strv default_opts;

static inline uint64_t rdtsc(void)
{
	unsigned int lo, hi;
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}

static inline bool opts_has_filter(struct motrace_opts *opts)
{
	return opts->filter || opts->trigger || opts->threshold || opts->depth != OPT_DEPTH_DEFAULT;
}

static inline bool has_perf_data(struct motrace_data *handle)
{
	return handle->perf != NULL;
}

static inline bool has_event_data(struct motrace_data *handle)
{
	return handle->perf_event_processed;
}

int command_report(int argc, char *argv[], struct motrace_opts *opts);
int command_info(int argc, char *argv[], struct motrace_opts *opts);
int command_graph(int argc, char *argv[], struct motrace_opts *opts);
int command_tui(int argc, char *argv[], struct motrace_opts *opts);
int command_nm(int argc, char *argv[], struct motrace_opts *opts);
int command_record(int argc, char *argv[], struct motrace_opts *opts);
int command_attach(int argc, char *argv[], struct motrace_opts *opts);
int command_stats(int argc, char *argv[], struct motrace_opts *opts);

extern volatile bool motrace_done;

int open_data_file(struct motrace_opts *opts, struct motrace_data *handle);
int open_info_file(struct motrace_opts *opts, struct motrace_data *handle);
void __close_data_file(struct motrace_opts *opts, struct motrace_data *handle, bool unload_modules);
static inline void close_data_file(struct motrace_opts *opts, struct motrace_data *handle)
{
	__close_data_file(opts, handle, true);
}

int read_task_file(struct motrace_session_link *sess, char *dirname, bool needs_symtab,
		   bool sym_rel_addr, bool needs_srcline);
int read_task_txt_file(struct motrace_session_link *sess, char *dirname, char *symdir,
		       bool needs_symtab, bool sym_rel_addr, bool needs_srcline);

char *get_libmcount_path(struct motrace_opts *opts);
void put_libmcount_path(char *libpath);

#define SESSION_ID_LEN 16
#define TASK_COMM_LEN 16
#define TASK_COMM_LAST (TASK_COMM_LEN - 1)
#define TASK_ID_LEN 7

struct motrace_session {
	struct rb_node node;
	char sid[SESSION_ID_LEN];
	uint64_t start_time;
	int pid, tid;
	struct motrace_sym_info sym_info;
	struct rb_root filters;
	struct rb_root fixups;
	struct list_head dlopen_libs;
	int namelen;
	char exename[];
};

struct motrace_sess_ref {
	struct motrace_sess_ref *next;
	struct motrace_session *sess;
	uint64_t start, end;
};

struct motrace_dlopen_list {
	struct list_head list;
	uint64_t time;
	unsigned long base;
	struct motrace_module *mod;
	struct rb_root filters;
};

struct motrace_task {
	int pid, tid, ppid;
	char comm[TASK_COMM_LEN];
	struct rb_node node;
	struct motrace_sess_ref sref;
	struct motrace_sess_ref *sref_last;
	struct list_head children;
	struct list_head siblings;
	struct {
		uint64_t run;
		uint64_t idle;
		uint64_t stamp;
	} time;
};

#define MOTRACE_MSG_MAGIC 0xface

enum motrace_msg_type {
	MOTRACE_MSG_REC_START = 1,
	MOTRACE_MSG_REC_END,
	MOTRACE_MSG_TASK_START,
	MOTRACE_MSG_TASK_END,
	MOTRACE_MSG_FORK_START,
	MOTRACE_MSG_FORK_END,
	MOTRACE_MSG_SESSION,
	MOTRACE_MSG_LOST,
	MOTRACE_MSG_DLOPEN,
	MOTRACE_MSG_FINISH,

	MOTRACE_MSG_SEND_START = 100,
	MOTRACE_MSG_SEND_DIR_NAME,
	MOTRACE_MSG_SEND_DATA,
	MOTRACE_MSG_SEND_KERNEL_DATA,
	MOTRACE_MSG_SEND_PERF_DATA,
	MOTRACE_MSG_SEND_INFO,
	MOTRACE_MSG_SEND_META_DATA,
	MOTRACE_MSG_SEND_END,

	MOTRACE_MSG_AGENT_CLOSE = 200, /* close the connection */
	MOTRACE_MSG_AGENT_QUERY, /* perform connection handshake */
	MOTRACE_MSG_AGENT_GET_OPT, /* get current option value */
	MOTRACE_MSG_AGENT_SET_OPT, /* set new option value */
	MOTRACE_MSG_AGENT_OK, /* ack previous message */
	MOTRACE_MSG_AGENT_ERR, /* signal error on previous message */
};

/* msg format for communicating by pipe */
struct motrace_msg {
	unsigned short magic; /* MOTRACE_MSG_MAGIC */
	unsigned short type; /* MOTRACE_MSG_* */
	unsigned int len;
	unsigned char data[];
};

struct motrace_msg_task {
	uint64_t time;
	int32_t pid;
	int32_t tid;
};

struct motrace_msg_sess {
	struct motrace_msg_task task;
	char sid[16];
	int unused;
	int namelen;
	char exename[];
};

struct motrace_msg_dlopen {
	struct motrace_msg_task task;
	uint64_t base_addr;
	char sid[16];
	int unused;
	int namelen;
	char exename[];
};

enum motrace_agent_opt {
	MOTRACE_AGENT_OPT_TRACE = (1U << 0), /* turn tracing on/off */
	MOTRACE_AGENT_OPT_DEPTH = (1U << 1), /* mcount depth filter */
	MOTRACE_AGENT_OPT_THRESHOLD = (1U << 2), /* mcount time filter */
	MOTRACE_AGENT_OPT_PATTERN = (1U << 3), /* pattern match type */
	MOTRACE_AGENT_OPT_FILTER = (1U << 4), /* tracing filters */
	MOTRACE_AGENT_OPT_CALLER = (1U << 5), /* tracing caller filters */
	MOTRACE_AGENT_OPT_TRIGGER = (1U << 6), /* tracing trigger actions */
	MOTRACE_AGENT_OPT_MO_ATTACH = (1U << 7), /* mo-mode attach (XRay sled patching) */
	MOTRACE_AGENT_OPT_MO_DETACH = (1U << 8), /* mo-mode detach */
};

#define MOTRACE_MO_ATTACH_F_OFFCPU (1U << 0)

struct motrace_agent_mo_attach {
	uint32_t flags;
	uint32_t bufsize; /* shmem buffer size in bytes */
	uint32_t max_stack;
	uint32_t reserved;
	char dirname[]; /* NUL-terminated */
};

extern struct motrace_session *first_session;

void create_session(struct motrace_session_link *sess, struct motrace_msg_sess *msg, char *dirname,
		    char *symdir, char *exename, bool sym_rel_addr, bool needs_symtab,
		    bool needs_srcline);
struct motrace_session *find_task_session(struct motrace_session_link *sess,
					  struct motrace_task *task, uint64_t timestamp);
void create_task(struct motrace_session_link *sess, struct motrace_msg_task *msg, bool fork);
struct motrace_task *find_task(struct motrace_session_link *sess, int tid);
void read_session_map(char *dirname, struct motrace_sym_info *sinfo, char *sid);
void delete_session_map(struct motrace_sym_info *sinfo);
void update_session_map(const char *filename);
struct motrace_session *get_session_from_sid(struct motrace_session_link *sess, char sid[]);
void session_add_dlopen(struct motrace_session *sess, uint64_t timestamp, unsigned long base_addr,
			const char *libname, bool needs_srcline);
void session_setup_dlopen_argspec(struct motrace_session *sess,
				  struct motrace_filter_setting *setting, bool is_retval);
struct motrace_dlopen_list *session_find_dlopen(struct motrace_session *sess, uint64_t timestamp,
						unsigned long addr);
struct motrace_symbol *session_find_dlsym(struct motrace_session *sess, uint64_t timestamp,
					  unsigned long addr);
struct motrace_filter *session_find_filter(struct motrace_session *sess, struct motrace_record *rec,
					   struct motrace_trigger *tr);
void delete_sessions(struct motrace_session_link *sess);

struct motrace_record;
struct motrace_symbol *task_find_sym(struct motrace_session_link *sess,
				     struct motrace_task_reader *task, struct motrace_record *rec);
struct motrace_symbol *task_find_sym_addr(struct motrace_session_link *sess,
					  struct motrace_task_reader *task, uint64_t time,
					  uint64_t addr);
struct motrace_dbg_loc *task_find_loc_addr(struct motrace_session_link *sess,
					   struct motrace_task_reader *task, uint64_t time,
					   uint64_t addr);

typedef int (*walk_sessions_cb_t)(struct motrace_session *session, void *arg);
void walk_sessions(struct motrace_session_link *sess, walk_sessions_cb_t callback, void *arg);
typedef int (*walk_tasks_cb_t)(struct motrace_task *task, void *arg);
void walk_tasks(struct motrace_session_link *sess, walk_tasks_cb_t callback, void *arg);

int setup_client_socket(struct motrace_opts *opts);
void send_trace_dir_name(int sock, char *name);
void send_trace_data(int sock, int tid, void *data, size_t len);
void send_trace_kernel_data(int sock, int cpu, void *data, size_t len);
void send_trace_perf_data(int sock, int cpu, void *data, size_t len);
void send_trace_metadata(int sock, const char *dirname, char *filename);
void send_trace_info(int sock, struct motrace_file_header *hdr, void *info, int len);
void send_trace_end(int sock);

void write_task_info(const char *dirname, struct motrace_msg_task *tmsg);
void write_fork_info(const char *dirname, struct motrace_msg_task *tmsg);
void write_session_info(const char *dirname, struct motrace_msg_sess *smsg, const char *exename);
void write_dlopen_info(const char *dirname, struct motrace_msg_dlopen *dmsg, const char *libname);

enum motrace_record_type {
	MOTRACE_ENTRY,
	MOTRACE_EXIT,
	MOTRACE_LOST,
	MOTRACE_EVENT,
};

#define RECORD_MAGIC_V3 0xa
#define RECORD_MAGIC_V4 0x5
#define RECORD_MAGIC RECORD_MAGIC_V4

/* reduced version of mcount_ret_stack */
struct motrace_record {
	uint64_t time;
	uint64_t type : 2;
	uint64_t more : 1;
	uint64_t magic : 3;
	uint64_t depth : 10;
	uint64_t addr : 48; /* child ip or motrace_event_id */
};

/* "more" payload header for ENTRY/EXIT records (used when feat_mask has OFFCPU) */
#define MOTRACE_MOREDATA_MAGIC 0x4f43 /* 'O''C' */

enum motrace_mored_data_flags {
	MOTRACE_MOREDATA_ARGS = (1U << 0),
	MOTRACE_MOREDATA_RETVAL = (1U << 1),
	MOTRACE_MOREDATA_CPUTIME = (1U << 2),
};

struct motrace_mored_data {
	uint16_t magic;
	uint16_t flags;
	uint16_t args_len;
	uint16_t retval_len;
};

static inline bool is_v3_compat(struct motrace_record *urec)
{
	/* (RECORD_MAGIC_V4 << 1 | more) == RECORD_MAGIC_V3 */
	return urec->magic == RECORD_MAGIC && urec->more == 0;
}

struct motrace_fstack_args {
	struct list_head *args;
	unsigned len;
	void *data;
	uint64_t cpu_time;
};

struct motrace_rstack_list {
	struct list_head read;
	struct list_head unused;
	int count;
};

struct motrace_rstack_list_node {
	struct list_head list;
	struct motrace_record rstack;
	struct motrace_fstack_args args;
};

void setup_rstack_list(struct motrace_rstack_list *list);
void add_to_rstack_list(struct motrace_rstack_list *list, struct motrace_record *rstack,
			struct motrace_fstack_args *args);
struct motrace_record *get_first_rstack_list(struct motrace_rstack_list *);
void consume_first_rstack_list(struct motrace_rstack_list *list);
void delete_last_rstack_list(struct motrace_rstack_list *list);
void reset_rstack_list(struct motrace_rstack_list *list);

enum motrace_ext_type {
	FTRACE_ARGUMENT = 1,
};

struct rusage;

int fill_file_header(struct motrace_opts *opts, int status, struct rusage *rusage,
		     char *elapsed_time);
void fill_motrace_info(uint64_t *info_mask, int fd, struct motrace_opts *opts, int status,
		       struct rusage *rusage, char *elapsed_time);
int read_motrace_info(uint64_t info_mask, struct motrace_data *handle);
void process_motrace_info(struct motrace_data *handle, struct motrace_opts *opts,
			  void (*process)(void *data, const char *fmt, ...), void *data);
void clear_motrace_info(struct motrace_info *info);

int arch_fill_cpuinfo_model(int fd);

enum motrace_event_id {
	EVENT_ID_KERNEL = 0U,
	/* kernel IDs are read from tracefs */

	EVENT_ID_BUILTIN = 100000U,
	EVENT_ID_READ_PROC_STATM,
	EVENT_ID_READ_PAGE_FAULT,
	EVENT_ID_DIFF_PROC_STATM,
	EVENT_ID_DIFF_PAGE_FAULT,
	EVENT_ID_READ_PMU_CYCLE,
	EVENT_ID_DIFF_PMU_CYCLE,
	EVENT_ID_READ_PMU_CACHE,
	EVENT_ID_DIFF_PMU_CACHE,
	EVENT_ID_READ_PMU_BRANCH,
	EVENT_ID_DIFF_PMU_BRANCH,
	EVENT_ID_WATCH_CPU,
	EVENT_ID_WATCH_VAR,

	/* supported perf events */
	EVENT_ID_PERF = 200000U,
	EVENT_ID_PERF_SCHED_IN,
	EVENT_ID_PERF_SCHED_OUT,
	EVENT_ID_PERF_SCHED_BOTH,
	EVENT_ID_PERF_TASK,
	EVENT_ID_PERF_EXIT,
	EVENT_ID_PERF_COMM,
	EVENT_ID_PERF_SCHED_OUT_PREEMPT,
	EVENT_ID_PERF_SCHED_BOTH_PREEMPT,

	EVENT_ID_USER = 1000000U,

	EVENT_ID_EXTERN_DATA = 2000000U,
};

struct motrace_event {
	struct list_head list;
	enum motrace_event_id id;
	char *provider;
	char *event;
};

struct motrace_watch_event {
	union {
		int cpu;
		struct {
			uint64_t addr;
			uint64_t data;
		} var;
	};
};

#define HTML_HEADER                                                                                \
	"<html>\n"                                                                                 \
	"<head></head>\n"                                                                          \
	"<body style='background-color:black;color:white;'>\n"                                     \
	"<pre>\n"

#define HTML_FOOTER                                                                                \
	"</pre>\n"                                                                                 \
	"</body>\n"                                                                                \
	"</html>\n"

/* for unit tests */
int prepare_test_data(struct motrace_opts *opts, struct motrace_data *handle);
int release_test_data(struct motrace_opts *opts, struct motrace_data *handle);

#endif /* MOTRACE_H */
