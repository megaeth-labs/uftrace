#ifndef MOTRACE_ARGSPEC_H
#define MOTRACE_ARGSPEC_H

#include "utils/list.h"
#include "utils/rbtree.h"
#include <stdbool.h>
#include <stdio.h>

enum motrace_arg_format {
	ARG_FMT_AUTO,
	ARG_FMT_SINT,
	ARG_FMT_UINT,
	ARG_FMT_HEX,
	ARG_FMT_OCT,
	ARG_FMT_STR,
	ARG_FMT_CHAR,
	ARG_FMT_FLOAT,
	ARG_FMT_STD_STRING,
	ARG_FMT_PTR,
	ARG_FMT_ENUM,
	ARG_FMT_STRUCT,
};

#define ARG_TYPE_INDEX 0
#define ARG_TYPE_FLOAT 1
#define ARG_TYPE_REG 2
#define ARG_TYPE_STACK 3

/* should match with motrace_arg_format above */
#define ARG_SPEC_CHARS "diuxoscfSpet"

/**
 * motrace_arg_spec contains arguments and return value info.
 *
 * If idx is zero, it means the recorded data is return value.
 *
 * If idx is not zero, it means the recorded data is arguments
 * and idx shows the sequence order of arguments.
 */
#define RETVAL_IDX 0

struct motrace_arg_spec {
	struct list_head list;
	int idx;
	enum motrace_arg_format fmt;
	int size;
	bool exact;
	unsigned char type;
	short struct_reg_cnt;
	union {
		short reg_idx;
		short stack_ofs;
	};
	char *type_name;
	short struct_regs[4];
};

struct motrace_filter_setting;

struct motrace_arg_spec *parse_argspec(char *str, struct motrace_filter_setting *setting);

void setup_auto_args(struct motrace_filter_setting *setting);
void setup_auto_args_str(char *args, char *rets, char *enums,
			 struct motrace_filter_setting *setting);
void finish_auto_args(void);

void free_arg_spec(struct motrace_arg_spec *arg);

struct motrace_dbg_info;
struct motrace_filter;
struct motrace_trigger;

struct motrace_filter *find_auto_argspec(struct motrace_filter *filter, struct motrace_trigger *tr,
					 struct motrace_dbg_info *dinfo,
					 struct motrace_filter_setting *setting);
struct motrace_filter *find_auto_retspec(struct motrace_filter *filter, struct motrace_trigger *tr,
					 struct motrace_dbg_info *dinfo,
					 struct motrace_filter_setting *setting);
char *get_auto_argspec_str(void);
char *get_auto_retspec_str(void);
char *get_auto_enum_str(void);
int extract_trigger_args(char **pargs, char **prets, char *trigger);
int parse_enum_string(char *enum_str, struct rb_root *root);
char *get_enum_string(struct rb_root *root, char *name, long val);
void save_enum_def(struct rb_root *root, FILE *fp);
void release_enum_def(struct rb_root *root);

extern struct rb_root dwarf_enum;

#endif /* MOTRACE_ARGSPEC_H */
