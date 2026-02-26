#include "motrace.h"
#include "utils/perf.h"

int setup_perf_data(struct motrace_data *handle)
{
	if (handle) {
		handle->hdr.feat_mask &= ~PERF_EVENT;
		handle->nr_perf = 0;
		handle->perf = NULL;
	}
	return -1;
}

void finish_perf_data(struct motrace_data *handle)
{
	(void)handle;
}

int read_perf_data(struct motrace_data *handle)
{
	(void)handle;
	return -1;
}

struct motrace_record *get_perf_record(struct motrace_data *handle,
				       struct motrace_perf_reader *perf)
{
	(void)handle;
	(void)perf;
	return NULL;
}

void update_perf_task_comm(struct motrace_data *handle)
{
	(void)handle;
}

void process_perf_event(struct motrace_data *handle)
{
	if (handle)
		handle->perf_event_processed = true;
}
