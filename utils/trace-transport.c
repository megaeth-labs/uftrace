#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include "motrace.h"
#include "utils/utils.h"

int setup_client_socket(struct motrace_opts *opts)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(opts->port),
	};
	struct addrinfo *results, hints;
	int err;
	int sock;
	int one = 1;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = addr.sin_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	err = getaddrinfo(opts->host, NULL, &hints, &results);
	if (err)
		pr_err_ns("Failed to resolve host %s: %s\n", opts->host, gai_strerror(err));

	addr.sin_addr = ((struct sockaddr_in *)results->ai_addr)->sin_addr;
	freeaddrinfo(results);

	sock = socket(hints.ai_family, hints.ai_socktype, 0);
	if (sock < 0)
		pr_err("socket create failed");

	if (setsockopt(sock, SOL_TCP, TCP_NODELAY, &one, sizeof(one)) != 0)
		pr_warn("socket setting failed\n");

	if (connect(sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0)
		pr_err("socket connect failed (host: %s, port: %d)", opts->host, opts->port);

	return sock;
}

void send_trace_dir_name(int sock, char *name)
{
	ssize_t len = strlen(name);
	struct motrace_msg msg = {
		.magic = htons(MOTRACE_MSG_MAGIC),
		.type = htons(MOTRACE_MSG_SEND_DIR_NAME),
		.len = htonl(len),
	};
	struct iovec iov[] = {
		{
			.iov_base = &msg,
			.iov_len = sizeof(msg),
		},
		{
			.iov_base = name,
			.iov_len = len,
		},
	};

	pr_dbg2("send MOTRACE_MSG_SEND_HDR\n");
	if (writev_all(sock, iov, ARRAY_SIZE(iov)) < 0)
		pr_err("send header failed");
}

void send_trace_data(int sock, int tid, void *data, size_t len)
{
	int32_t msg_tid = htonl(tid);
	struct motrace_msg msg = {
		.magic = htons(MOTRACE_MSG_MAGIC),
		.type = htons(MOTRACE_MSG_SEND_DATA),
		.len = htonl(sizeof(msg_tid) + len),
	};
	struct iovec iov[] = {
		{
			.iov_base = &msg,
			.iov_len = sizeof(msg),
		},
		{
			.iov_base = &msg_tid,
			.iov_len = sizeof(msg_tid),
		},
		{
			.iov_base = data,
			.iov_len = len,
		},
	};

	pr_dbg2("send MOTRACE_MSG_SEND_DATA\n");
	if (writev_all(sock, iov, ARRAY_SIZE(iov)) < 0)
		pr_err("send data failed");
}

void send_trace_kernel_data(int sock, int cpu, void *data, size_t len)
{
	int32_t msg_cpu = htonl(cpu);
	struct motrace_msg msg = {
		.magic = htons(MOTRACE_MSG_MAGIC),
		.type = htons(MOTRACE_MSG_SEND_KERNEL_DATA),
		.len = htonl(sizeof(msg_cpu) + len),
	};
	struct iovec iov[] = {
		{
			.iov_base = &msg,
			.iov_len = sizeof(msg),
		},
		{
			.iov_base = &msg_cpu,
			.iov_len = sizeof(msg_cpu),
		},
		{
			.iov_base = data,
			.iov_len = len,
		},
	};

	pr_dbg2("send MOTRACE_MSG_SEND_KERNEL_DATA\n");
	if (writev_all(sock, iov, ARRAY_SIZE(iov)) < 0)
		pr_err("send kernel data failed");
}

void send_trace_perf_data(int sock, int cpu, void *data, size_t len)
{
	int32_t msg_cpu = htonl(cpu);
	struct motrace_msg msg = {
		.magic = htons(MOTRACE_MSG_MAGIC),
		.type = htons(MOTRACE_MSG_SEND_PERF_DATA),
		.len = htonl(sizeof(msg_cpu) + len),
	};
	struct iovec iov[] = {
		{
			.iov_base = &msg,
			.iov_len = sizeof(msg),
		},
		{
			.iov_base = &msg_cpu,
			.iov_len = sizeof(msg_cpu),
		},
		{
			.iov_base = data,
			.iov_len = len,
		},
	};

	pr_dbg2("send MOTRACE_MSG_SEND_PERF_DATA\n");
	if (writev_all(sock, iov, ARRAY_SIZE(iov)) < 0)
		pr_err("send kernel data failed");
}

void send_trace_metadata(int sock, const char *dirname, char *filename)
{
	int fd;
	void *buf;
	size_t len;
	char *pathname = NULL;
	struct stat stbuf;
	int32_t namelen = strlen(filename);
	struct motrace_msg msg = {
		.magic = htons(MOTRACE_MSG_MAGIC),
		.type = htons(MOTRACE_MSG_SEND_META_DATA),
		.len = sizeof(namelen) + namelen,
	};
	struct iovec iov[4] = {
		{
			.iov_base = &msg,
			.iov_len = sizeof(msg),
		},
		{
			.iov_base = &namelen,
			.iov_len = sizeof(namelen),
		},
		{
			.iov_base = filename,
			.iov_len = namelen,
		},
		{ /* to be filled */ },
	};

	if (dirname)
		xasprintf(&pathname, "%s/%s", dirname, filename);
	else
		pathname = xstrdup(filename);

	fd = open(pathname, O_RDONLY);
	if (fd < 0)
		pr_err("open %s failed", pathname);

	if (fstat(fd, &stbuf) < 0)
		pr_err("stat %s failed", pathname);

	len = stbuf.st_size;
	buf = xmalloc(len);

	msg.len = htonl(msg.len + len);
	iov[3].iov_base = buf;
	iov[3].iov_len = len;

	if (read_all(fd, buf, len) < 0)
		pr_err("map read failed");

	namelen = htonl(namelen);

	pr_dbg2("send MOTRACE_MSG_SEND_META_DATA: %s\n", filename);
	if (writev_all(sock, iov, ARRAY_SIZE(iov)) < 0)
		pr_err("send metadata failed");

	free(pathname);
	free(buf);
	close(fd);
}

void send_trace_info(int sock, struct motrace_file_header *hdr, void *info, int len)
{
	struct motrace_msg msg = {
		.magic = htons(MOTRACE_MSG_MAGIC),
		.type = htons(MOTRACE_MSG_SEND_INFO),
		.len = htonl(sizeof(*hdr) + len),
	};
	struct iovec iov[] = {
		{
			.iov_base = &msg,
			.iov_len = sizeof(msg),
		},
		{
			.iov_base = hdr,
			.iov_len = sizeof(*hdr),
		},
		{
			.iov_base = info,
			.iov_len = len,
		},
	};

	hdr->version = htonl(hdr->version);
	hdr->header_size = htons(hdr->header_size);
	hdr->feat_mask = htonq(hdr->feat_mask);
	hdr->info_mask = htonq(hdr->info_mask);
	hdr->max_stack = htons(hdr->max_stack);

	pr_dbg2("send MOTRACE_MSG_SEND_INFO\n");
	if (writev_all(sock, iov, ARRAY_SIZE(iov)) < 0)
		pr_err("send metadata failed");
}

void send_trace_end(int sock)
{
	struct motrace_msg msg = {
		.magic = htons(MOTRACE_MSG_MAGIC),
		.type = htons(MOTRACE_MSG_SEND_END),
	};

	pr_dbg2("send MOTRACE_MSG_SEND_END\n");
	if (write_all(sock, &msg, sizeof(msg)) < 0)
		pr_err("send end failed");
}
