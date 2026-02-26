#ifndef MOTRACE_SHMEM_H
#define MOTRACE_SHMEM_H

#include <sys/stat.h>

int motrace_shmem_open(const char *name, int oflag, mode_t mode);
int motrace_shmem_unlink(const char *name);
const char *motrace_shmem_root(void);

#endif /* MOTRACE_SHMEM_H */
