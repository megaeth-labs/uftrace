#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils/shmem.h"

#ifdef __ANDROID__

const char *motrace_shmem_root(void)
{
	static char motrace_dir[PATH_MAX] = "";

	if (motrace_dir[0] == 0) {
		const char *tmpdir;
		tmpdir = getenv("TMPDIR");
		if (!tmpdir)
			tmpdir = "/tmp";

		snprintf(motrace_dir, sizeof(motrace_dir), "%s/motrace", tmpdir);
	}

	return motrace_dir;
}

int motrace_shmem_open(const char *name, int oflag, mode_t mode)
{
	const char *motrace_dir;
	char *fname;
	int fd;
	int status;

	motrace_dir = motrace_shmem_root();

	status = mkdir(motrace_dir, mode);
	if (status < 0 && errno != EEXIST)
		return -1;

	if (asprintf(&fname, "%s/%s", motrace_dir, name) < 0)
		return -1;

	fd = open(fname, oflag, mode);
	if (fd >= 0) {
		int flags = fcntl(fd, F_GETFD, 0);
		flags |= FD_CLOEXEC;
		if (fcntl(fd, F_SETFD, flags) < 0) {
			int saved_errno = errno;
			close(fd);
			fd = -1;
			errno = saved_errno;
		}
	}

	free(fname);

	return fd;
}

int motrace_shmem_unlink(const char *name)
{
	const char *motrace_dir;
	char *fname;
	int status;

	motrace_dir = motrace_shmem_root();

	if (asprintf(&fname, "%s/%s", motrace_dir, name))
		return -1;
	status = unlink(fname);
	free(fname);

	return status;
}

#else /* ! __ANDROID__ */

#include <sys/mman.h>

enum motrace_shmem_method {
	MOTRACE_SHMEM_UNKNOWN,
	MOTRACE_SHMEM_POSIX,
	MOTRACE_SHMEM_FILE,
};

static enum motrace_shmem_method shmem_method = MOTRACE_SHMEM_UNKNOWN;

static const char *motrace_fallback_shmem_root(void)
{
	static char motrace_dir[PATH_MAX] = "";

	if (motrace_dir[0] == 0) {
		const char *tmpdir;

		tmpdir = getenv("TMPDIR");
		if (!tmpdir)
			tmpdir = "/tmp";

		snprintf(motrace_dir, sizeof(motrace_dir), "%s/motrace", tmpdir);
	}

	return motrace_dir;
}

const char *motrace_shmem_root(void)
{
	if (shmem_method == MOTRACE_SHMEM_FILE)
		return motrace_fallback_shmem_root();

	return "/dev/shm";
}

static int open_fallback_shmem(const char *name, int oflag, mode_t mode)
{
	const char *motrace_dir;
	char *fname;
	int fd;

	motrace_dir = motrace_fallback_shmem_root();

	if (mkdir(motrace_dir, 0775) < 0 && errno != EEXIST)
		return -1;

	if (asprintf(&fname, "%s/%s", motrace_dir, name) < 0)
		return -1;

	fd = open(fname, oflag, mode);
	if (fd >= 0) {
		int flags = fcntl(fd, F_GETFD, 0);

		flags |= FD_CLOEXEC;
		if (fcntl(fd, F_SETFD, flags) < 0) {
			int saved_errno = errno;

			close(fd);
			fd = -1;
			errno = saved_errno;
		}
	}

	free(fname);
	return fd;
}

static int unlink_fallback_shmem(const char *name)
{
	const char *motrace_dir;
	char *fname;
	int status;

	motrace_dir = motrace_fallback_shmem_root();

	if (asprintf(&fname, "%s/%s", motrace_dir, name) < 0)
		return -1;

	status = unlink(fname);
	free(fname);
	return status;
}

int motrace_shmem_open(const char *name, int oflag, mode_t mode)
{
	int fd;

	if (shmem_method != MOTRACE_SHMEM_FILE) {
		fd = shm_open(name, oflag, mode);
		if (fd >= 0) {
			shmem_method = MOTRACE_SHMEM_POSIX;
			return fd;
		}

		if (errno == EACCES || errno == EPERM) {
			shmem_method = MOTRACE_SHMEM_FILE;
			return open_fallback_shmem(name, oflag, mode);
		}
	}

	return open_fallback_shmem(name, oflag, mode);
}

int motrace_shmem_unlink(const char *name)
{
	if (shmem_method != MOTRACE_SHMEM_FILE)
		return shm_unlink(name);

	return unlink_fallback_shmem(name);
}

#endif
