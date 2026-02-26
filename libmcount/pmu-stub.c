#include "libmcount/internal.h"

int read_pmu_event(struct mcount_thread_data *mtdp, enum motrace_event_id id, void *buf)
{
	(void)mtdp;
	(void)id;
	(void)buf;
	return -1;
}

void release_pmu_event(struct mcount_thread_data *mtdp, enum motrace_event_id id)
{
	(void)mtdp;
	(void)id;
}

void finish_pmu_event(struct mcount_thread_data *mtdp)
{
	(void)mtdp;
}
