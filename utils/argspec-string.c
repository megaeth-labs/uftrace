#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "motrace.h"
#include "utils/argspec.h"
#include "utils/fstack.h"
#include "utils/list.h"
#include "utils/utils.h"

static void print_char(char **args, size_t *len, const char c)
{
	**args = c;
	*args += 1;
	*len -= 1;
}

static void print_args(char **args, size_t *len, const char *fmt, ...)
{
	int x;
	va_list ap;

	va_start(ap, fmt);
	x = vsnprintf(*args, *len, fmt, ap);
	va_end(ap);
	*args += x;
	*len -= x;
}

void print_json_escaped_char(char **args, size_t *len, const char c)
{
	if (c == '\n')
		print_args(args, len, "\\\\n");
	else if (c == '\t')
		print_args(args, len, "\\\\t");
	else if (c == '\\')
		print_args(args, len, "\\\\");
	else if (c == '"')
		print_args(args, len, "\\\"");
	else if (isprint(c))
		print_char(args, len, c);
	else
		print_args(args, len, "\\\\x%02hhx", c);
}

static void print_escaped_char(char **args, size_t *len, const char c)
{
	if (c == '\0')
		print_args(args, len, "\\0");
	else if (c == '\b')
		print_args(args, len, "\\b");
	else if (c == '\n')
		print_args(args, len, "\\n");
	else
		print_char(args, len, c);
}

void get_argspec_string(struct motrace_task_reader *task, char *args, size_t len,
			enum motrace_argspec_string_bits str_mode)
{
	int i = 0, n = 0;
	char *str = NULL;

	const int null_str = -1;
	void *data = task->args.data;
	struct list_head *arg_list = task->args.args;
	struct motrace_arg_spec *spec;
	union {
		long i;
		void *p;
		float f;
		double d;
		long long ll;
		long double D;
		unsigned char v[16];
	} val;

	bool needs_paren = !!(str_mode & NEEDS_PAREN);
	bool needs_semi_colon = !!(str_mode & NEEDS_SEMI_COLON);
	bool has_more = !!(str_mode & HAS_MORE);
	bool is_retval = !!(str_mode & IS_RETVAL);
	bool needs_assignment = !!(str_mode & NEEDS_ASSIGNMENT);
	bool needs_json = !!(str_mode & NEEDS_JSON);

	if (!has_more) {
		if (needs_paren)
			strcpy(args, "()");
		else {
			if (is_retval && needs_semi_colon)
				args[n++] = ';';
			args[n] = '\0';
		}
		return;
	}

	ASSERT(arg_list && !list_empty(arg_list));

	if (needs_paren)
		print_args(&args, &len, "(");
	else if (needs_assignment)
		print_args(&args, &len, " = ");

	list_for_each_entry(spec, arg_list, list) {
		char fmtstr[16];
		char *len_mod[] = { "hh", "h", "", "ll" };
		char fmt, *lm;
		unsigned idx;
		size_t size = spec->size;

		/* skip unwanted arguments or retval */
		if (is_retval != (spec->idx == RETVAL_IDX))
			continue;

		if (i > 0)
			print_args(&args, &len, ", ");

		memset(val.v, 0, sizeof(val));
		fmt = ARG_SPEC_CHARS[spec->fmt];

		switch (spec->fmt) {
		case ARG_FMT_AUTO:
			memcpy(val.v, data, spec->size);
			if (val.i > 100000L || val.i < -100000L) {
				fmt = 'x';
				/*
				 * Show small negative integers naturally
				 * on 64-bit systems.  The conversion is
				 * required to avoid compiler warnings
				 * on 32-bit systems.
				 */
				if (sizeof(long) == sizeof(uint64_t)) {
					uint64_t val64 = val.i;

					if (val64 > 0xffff0000 && val64 <= 0xffffffff) {
						fmt = 'd';
						idx = 2;
						break;
					}
				}
			}
			/* fall through */
		case ARG_FMT_SINT:
		case ARG_FMT_HEX:
		case ARG_FMT_OCT:
			idx = ffs(spec->size) - 1;
			break;
		case ARG_FMT_UINT:
			memcpy(val.v, data, spec->size);
			if ((unsigned long)val.i > 100000UL)
				fmt = 'x';
			idx = ffs(spec->size) - 1;
			break;
		default:
			idx = 2;
			break;
		}

		if (spec->fmt == ARG_FMT_STR || spec->fmt == ARG_FMT_STD_STRING) {
			unsigned short slen;

			memcpy(&slen, data, 2);

			str = xmalloc(slen + 1);
			memcpy(str, data + 2, slen);
			str[slen] = '\0';

			if (slen == 4 && !memcmp(str, &null_str, sizeof(null_str)))
				print_args(&args, &len, "NULL");
			else if (needs_json) {
				char *p = str;

				print_args(&args, &len, "\\\"");
				while (*p) {
					char c = *p++;
					print_json_escaped_char(&args, &len, c);
				}
				print_args(&args, &len, "\\\"");
			}
			else {
				char *p = str;

				print_args(&args, &len, "%s\"", color_string);
				while (*p) {
					char c = *p++;
					if (c & 0x80) {
						break;
					}
				}
				/*
				 * if value of character is over than 128(0x80),
				 * then it will be UTF-8 string
				 */
				if (*p) {
					print_args(&args, &len, "%.*s", slen, str);
				}
				else {
					p = str;
					while (*p) {
						char c = *p++;
						print_escaped_char(&args, &len, c);
					}
				}

				print_args(&args, &len, "\"%s", color_reset);
			}

			/* std::string can be represented as "TEXT"s from C++14 */
			if (spec->fmt == ARG_FMT_STD_STRING)
				print_args(&args, &len, "s");

			free(str);
			size = slen + 2;
		}
		else if (spec->fmt == ARG_FMT_CHAR) {
			char c;

			memcpy(&c, data, 1);
			if (needs_json) {
				print_args(&args, &len, "'");
				print_json_escaped_char(&args, &len, c);
				print_args(&args, &len, "'");
			}
			else {
				print_args(&args, &len, "%s", color_string);
				print_args(&args, &len, "'");
				print_escaped_char(&args, &len, c);
				print_args(&args, &len, "'");
				print_args(&args, &len, "%s", color_reset);
			}
			size = 1;
		}
		else if (spec->fmt == ARG_FMT_FLOAT) {
			if (spec->size == 10)
				lm = "L";
			else
				lm = len_mod[idx];

			memcpy(val.v, data, spec->size);
			snprintf(fmtstr, sizeof(fmtstr), "%%#%s%c", lm, fmt);

			switch (spec->size) {
			case 4:
				print_args(&args, &len, fmtstr, val.f);
				break;
			case 8:
				print_args(&args, &len, fmtstr, val.d);
				break;
			case 10:
				print_args(&args, &len, fmtstr, val.D);
				break;
			default:
				pr_dbg("invalid floating-point type size %d\n", spec->size);
				break;
			}
		}
		else if (spec->fmt == ARG_FMT_PTR) {
			struct motrace_session_link *sessions = &task->h->sessions;
			struct motrace_symbol *sym;

			memcpy(val.v, data, spec->size);
			sym = task_find_sym_addr(sessions, task, task->rstack->time,
						 (uint64_t)val.i);

			if (sym) {
				print_args(&args, &len, "%s", color_symbol);
				if (format_mode == FORMAT_HTML)
					print_args(&args, &len, "&amp;%s", sym->name);
				else
					print_args(&args, &len, "&%s", sym->name);
				print_args(&args, &len, "%s", color_reset);
			}
			else if (val.p)
				print_args(&args, &len, "%p", val.p);
			else
				print_args(&args, &len, "0");
		}
		else if (spec->fmt == ARG_FMT_ENUM) {
			struct motrace_session_link *sessions = &task->h->sessions;
			struct motrace_session *s;
			struct motrace_mmap *map;
			struct motrace_dbg_info *dinfo;
			char *estr;

			memcpy(val.v, data, spec->size);
			s = find_task_session(sessions, task->t, task->rstack->time);

			map = find_map(&s->sym_info, task->rstack->addr);
			if (map == NULL || map->mod == NULL) {
				print_args(&args, &len, "<ENUM?> %x", (int)val.i);
				goto next;
			}

			dinfo = &map->mod->dinfo;
			estr = get_enum_string(&dinfo->enums, spec->type_name, val.i);
			if (strstr(estr, "|") && strcmp("|", color_enum_or)) {
				struct strv enum_vals = STRV_INIT;

				strv_split(&enum_vals, estr, "|");
				free(estr);
				estr = strv_join(&enum_vals, color_enum_or);
				strv_free(&enum_vals);
			}

			print_args(&args, &len, "%s", color_enum);
			if (strlen(estr) >= len)
				print_args(&args, &len, "<ENUM>");
			else
				print_args(&args, &len, "%s", estr);
			print_args(&args, &len, "%s", color_reset);
			free(estr);
		}
		else if (spec->fmt == ARG_FMT_STRUCT) {
			if (spec->type_name) {
				/*
				 * gcc puts "<lambda" to anonymous lambda
				 * but let's ignore to make it same as clang.
				 */
				if (strcmp(spec->type_name, "<lambda")) {
					print_args(&args, &len, "%s%s%s", color_struct,
						   spec->type_name, color_reset);
				}
			}
			if (spec->size)
				print_args(&args, &len, "{...}");
			else
				print_args(&args, &len, "{}");
		}
		else {
			if (spec->fmt != ARG_FMT_AUTO)
				memcpy(val.v, data, spec->size);

			ASSERT(idx < ARRAY_SIZE(len_mod));
			lm = len_mod[idx];

			snprintf(fmtstr, sizeof(fmtstr), "%%#%s%c", lm, fmt);
			if (spec->size == 8)
				print_args(&args, &len, fmtstr, val.ll);
			else
				print_args(&args, &len, fmtstr, val.i);
		}

next:
		i++;
		data += ALIGN(size, 4);

		if (len <= 2)
			break;

		/* read only the first match for retval */
		if (is_retval)
			break;
	}

	if (needs_paren) {
		print_args(&args, &len, ")");
	}
	else {
		if (needs_semi_colon)
			args[n++] = ';';
		args[n] = '\0';
	}
}
