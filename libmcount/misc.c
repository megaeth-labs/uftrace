#include <fcntl.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

/* This should be defined before #include "utils.h" */
#define PR_FMT "mcount"
#define PR_DOMAIN DBG_MCOUNT

#include "libmcount/internal.h"
#include "libmcount/mcount.h"
#include "utils/utils.h"

static char session_buf[2][SESSION_ID_LEN + 1];
static int session_idx;
/* 0 = uninit, 1 = initing, 2 = ready */
static int session_state;

void update_kernel_tid(int tid)
{
	(void)tid;
}

const char *mcount_session_name(void)
{
	/* ensure it has a valid session string */
	if (__atomic_load_n(&session_state, __ATOMIC_ACQUIRE) != 2) {
		int expected = 0;

		if (__atomic_compare_exchange_n(&session_state, &expected, 1, false,
						__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			uint64_t session_id = 0;
			int fd;

			fd = open("/dev/urandom", O_RDONLY);
			if (fd >= 0) {
				if (read(fd, &session_id, sizeof(session_id)) !=
				    (ssize_t)sizeof(session_id))
					pr_err("reading from urandom");

				close(fd);
			}
			else {
				srandom(time(NULL));
				session_id = random();
				session_id <<= 32;
				session_id |= random();
			}

			snprintf(session_buf[0], sizeof(session_buf[0]), "%0*" PRIx64,
				 SESSION_ID_LEN, session_id);
			__atomic_store_n(&session_idx, 0, __ATOMIC_RELEASE);
			__atomic_store_n(&session_state, 2, __ATOMIC_RELEASE);
		}
		else {
			/* wait for another thread to complete initialization */
			while (__atomic_load_n(&session_state, __ATOMIC_ACQUIRE) != 2)
				sched_yield();
		}
	}

	return session_buf[__atomic_load_n(&session_idx, __ATOMIC_ACQUIRE)];
}

void mcount_session_reset(void)
{
	int new_idx;
	uint64_t session_id = 0;
	int fd;

	/* ensure it has initial buffers/state */
	mcount_session_name();

	new_idx = __atomic_load_n(&session_idx, __ATOMIC_RELAXED) ^ 1;

	fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		if (read(fd, &session_id, sizeof(session_id)) != (ssize_t)sizeof(session_id))
			pr_err("reading from urandom");

		close(fd);
	}
	else {
		srandom(time(NULL));
		session_id = random();
		session_id <<= 32;
		session_id |= random();
	}

	snprintf(session_buf[new_idx], sizeof(session_buf[new_idx]), "%0*" PRIx64, SESSION_ID_LEN,
		 session_id);
	__atomic_store_n(&session_idx, new_idx, __ATOMIC_RELEASE);
}

void motrace_send_message(int type, void *data, size_t len)
{
	struct motrace_msg msg = {
		.magic = MOTRACE_MSG_MAGIC,
		.type = type,
		.len = len,
	};
	struct iovec iov[2] = {
		{
			.iov_base = &msg,
			.iov_len = sizeof(msg),
		},
		{
			.iov_base = data,
			.iov_len = len,
		},
	};

	if (mcount_pfd < 0)
		return;

	len += sizeof(msg);
	if (writev(mcount_pfd, iov, 2) != (ssize_t)len) {
		if (!mcount_should_stop())
			pr_err("send msg (type %d) failed", type);
	}
}

void build_debug_domain(char *dbg_domain_str)
{
	int i, len;

	if (dbg_domain_str == NULL)
		return;

	len = strlen(dbg_domain_str);
	for (i = 0; i < len; i += 2) {
		const char *pos;
		char domain = dbg_domain_str[i];
		int level = dbg_domain_str[i + 1] - '0';
		int d;

		pos = strchr(DBG_DOMAIN_STR, domain);
		if (pos == NULL)
			continue;

		d = pos - DBG_DOMAIN_STR;
		dbg_domain[d] = level;
	}
}

bool mcount_rstack_has_plthook(struct mcount_thread_data *mtdp)
{
	int idx;

	for (idx = 0; idx < mtdp->idx; idx++) {
		if (mtdp->rstack[idx].dyn_idx != MCOUNT_INVALID_DYNIDX)
			return true;
	}
	return false;
}

/* restore saved original return address */
void mcount_rstack_restore(struct mcount_thread_data *mtdp)
{
	int idx;
	struct mcount_ret_stack *rstack;
	unsigned long plthook_return_fn = (unsigned long)plthook_return;

	if (unlikely(mcount_estimate_return))
		return;

	/* reverse order due to tail calls */
	for (idx = mtdp->idx - 1; idx >= 0; idx--) {
		rstack = &mtdp->rstack[idx];

		if (rstack->parent_ip == mcount_return_fn || rstack->parent_ip == plthook_return_fn)
			continue;

		if (!ARCH_CAN_RESTORE_PLTHOOK && rstack->dyn_idx != MCOUNT_INVALID_DYNIDX) {
			/*
			 * We don't know exact location where the return address
			 * was saved (on ARM/AArch64).  But we know that the
			 * return address itself was changed to plthook_return_fn
			 * by the plt_hooker().  So it needs to scan the stack to
			 * look up the value.
			 */
			unsigned long *loc, *end;

			if (idx < mtdp->idx - 1) {
				struct mcount_ret_stack *next_rstack;

				next_rstack = rstack + 1;
				/* skip rstacks for -finstrument-functions */
				while (next_rstack->parent_loc == &mtdp->cygprof_dummy &&
				       next_rstack < &mtdp->rstack[mtdp->idx])
					next_rstack++;

				if (next_rstack == &mtdp->rstack[mtdp->idx])
					goto last_rstack;

				/* special case: same as tail-call */
				if (next_rstack->parent_ip == plthook_return_fn) {
					rstack->parent_loc = next_rstack->parent_loc;
					*rstack->parent_loc = rstack->parent_ip;
					continue;
				}

				end = next_rstack->parent_loc;
			}
			else {
last_rstack:
				/* just check 32 stack slots */
				end = rstack->parent_loc - 32;
			}

			for (loc = rstack->parent_loc; loc < end; loc--) {
				if (*loc != plthook_return_fn)
					continue;

				rstack->parent_loc = loc;
				*loc = rstack->parent_ip;
				break;
			}
			continue;
		}

		*rstack->parent_loc = rstack->parent_ip;
	}
}

/* hook return address again (used after mcount_rstack_restore) */
void mcount_rstack_rehook(struct mcount_thread_data *mtdp)
{
	int idx;
	struct mcount_ret_stack *rstack;

	if (unlikely(mcount_estimate_return))
		return;

	for (idx = mtdp->idx - 1; idx >= 0; idx--) {
		rstack = &mtdp->rstack[idx];

		if (rstack->dyn_idx == MCOUNT_INVALID_DYNIDX)
			*rstack->parent_loc = mcount_return_fn;
		else if (ARCH_CAN_RESTORE_PLTHOOK)
			*rstack->parent_loc = (unsigned long)plthook_return;
	}
}

void mcount_auto_restore(struct mcount_thread_data *mtdp)
{
	struct mcount_ret_stack *curr_rstack;
	struct mcount_ret_stack *prev_rstack;

	/* auto recover is meaningful only if parent rstack is hooked */
	if (mtdp->idx < 2)
		return;

	if (mtdp->in_exception)
		return;

	curr_rstack = &mtdp->rstack[mtdp->idx - 1];
	prev_rstack = &mtdp->rstack[mtdp->idx - 2];

	if (!ARCH_CAN_RESTORE_PLTHOOK && prev_rstack->dyn_idx != MCOUNT_INVALID_DYNIDX)
		return;

	/* ignore tail calls */
	if (curr_rstack->parent_loc == prev_rstack->parent_loc)
		return;

	while (prev_rstack >= mtdp->rstack) {
		unsigned long parent_ip = prev_rstack->parent_ip;

		/* parent also can be tail-called; skip */
		if (parent_ip == mcount_return_fn || parent_ip == (unsigned long)plthook_return) {
			prev_rstack--;
			continue;
		}

		*prev_rstack->parent_loc = parent_ip;
		return;
	}
}

void mcount_auto_rehook(struct mcount_thread_data *mtdp)
{
	struct mcount_ret_stack *curr_rstack;
	struct mcount_ret_stack *prev_rstack;

	/* auto recover is meaningful only if parent rstack is hooked */
	if (mtdp->idx < 2)
		return;

	if (mtdp->in_exception)
		return;

	curr_rstack = &mtdp->rstack[mtdp->idx - 1];
	prev_rstack = &mtdp->rstack[mtdp->idx - 2];

	if (!ARCH_CAN_RESTORE_PLTHOOK && prev_rstack->dyn_idx != MCOUNT_INVALID_DYNIDX)
		return;

	/* ignore tail calls */
	if (curr_rstack->parent_loc == prev_rstack->parent_loc)
		return;

	if (prev_rstack->dyn_idx == MCOUNT_INVALID_DYNIDX)
		*prev_rstack->parent_loc = mcount_return_fn;
	else
		*prev_rstack->parent_loc = (unsigned long)plthook_return;
}

#ifdef UNIT_TEST

TEST_CASE(mcount_debug_domain)
{
	int i;
	char dbg_str[DBG_DOMAIN_MAX * 2 + 1];

	/* ensure domain string matches to current domain bit */
	TEST_EQ(DBG_DOMAIN_MAX, (int)strlen(DBG_DOMAIN_STR));

	pr_dbg("initially all domains are off\n");
	for (i = 0; i < DBG_DOMAIN_MAX; i++) {
		if (i != PR_DOMAIN)
			TEST_EQ(dbg_domain[i], 0);
	}

	pr_dbg("turn on all domains\n");
	for (i = 0; i < DBG_DOMAIN_MAX; i++) {
		dbg_str[i * 2] = DBG_DOMAIN_STR[i];
		dbg_str[i * 2 + 1] = '1';
	}
	dbg_str[i * 2] = '\0';

	build_debug_domain(dbg_str);

	for (i = 0; i < DBG_DOMAIN_MAX; i++)
		TEST_EQ(dbg_domain[i], 1);

	/* increase mcount debug domain to 2 */
	strcpy(dbg_str, "M2");
	build_debug_domain(dbg_str);

	TEST_EQ(dbg_domain[PR_DOMAIN], 2);

	return TEST_OK;
}

#endif /* UNIT_TEST */
