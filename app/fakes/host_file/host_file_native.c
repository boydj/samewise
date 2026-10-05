/*
 * Runner-side half of host_file.h: built against the host C library.
 */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int wx_host_file_open_read(const char *path)
{
	return open(path, O_RDONLY | O_CLOEXEC);
}

int wx_host_file_open_write(const char *path)
{
	return open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
}

long wx_host_file_read(int fd, void *buf, unsigned long len)
{
	ssize_t n;

	do {
		n = read(fd, buf, len);
	} while (n < 0 && errno == EINTR);
	return (long)n;
}

long wx_host_file_write(int fd, const void *buf, unsigned long len)
{
	const char *p = buf;
	unsigned long done = 0;

	while (done < len) {
		ssize_t n = write(fd, p + done, len - done);

		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		done += (unsigned long)n;
	}
	return (long)done;
}

int wx_host_file_close(int fd)
{
	return close(fd);
}
