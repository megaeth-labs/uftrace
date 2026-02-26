#include "motrace.h"
#include "utils/kernel.h"

int setup_kernel_tracing(struct motrace_kernel_writer *kernel, struct motrace_opts *opts)
{
	(void)kernel;
	(void)opts;
	return -1;
}

int start_kernel_tracing(struct motrace_kernel_writer *kernel)
{
	(void)kernel;
	return -1;
}

int record_kernel_tracing(struct motrace_kernel_writer *kernel)
{
	(void)kernel;
	return -1;
}

int record_kernel_trace_pipe(struct motrace_kernel_writer *kernel, int cpu, int sock)
{
	(void)kernel;
	(void)cpu;
	(void)sock;
	return -1;
}

int stop_kernel_tracing(struct motrace_kernel_writer *kernel)
{
	(void)kernel;
	return 0;
}

int finish_kernel_tracing(struct motrace_kernel_writer *kernel)
{
	(void)kernel;
	return 0;
}

void list_kernel_events(void)
{
}

int setup_kernel_data(struct motrace_kernel_reader *kernel)
{
	(void)kernel;
	return -1;
}

int read_kernel_stack(struct motrace_data *handle, struct motrace_task_reader **taskp)
{
	(void)handle;
	(void)taskp;
	return -1;
}

int read_kernel_cpu_data(struct motrace_kernel_reader *kernel, int cpu)
{
	(void)kernel;
	(void)cpu;
	return -1;
}

void *read_kernel_event(struct motrace_kernel_reader *kernel, int cpu, int *psize)
{
	(void)kernel;
	(void)cpu;
	if (psize)
		*psize = 0;
	return NULL;
}

struct motrace_record *get_kernel_record(struct motrace_kernel_reader *kernel,
					 struct motrace_task_reader *task, int cpu)
{
	(void)kernel;
	(void)task;
	(void)cpu;
	return NULL;
}

int finish_kernel_data(struct motrace_kernel_reader *kernel)
{
	(void)kernel;
	return 0;
}

bool check_kernel_pid_filter(void)
{
	return false;
}
