/*
 * Host file access for native_sim fakes and tests.
 *
 * Implemented in the native simulator runner against the host C library,
 * so it works whichever libc the embedded image uses. Paths are host paths.
 */

#ifndef FAKES_HOST_FILE_H_
#define FAKES_HOST_FILE_H_

#ifdef __cplusplus
extern "C" {
#endif

/** Open for reading. Returns a descriptor >= 0, or -1. */
int wx_host_file_open_read(const char *path);

/** Create or truncate for writing (mode 0644). Returns a descriptor >= 0, or -1. */
int wx_host_file_open_write(const char *path);

/** Read up to len bytes. Returns the count, 0 at end of file, or -1. */
long wx_host_file_read(int fd, void *buf, unsigned long len);

/** Write len bytes (retrying short writes). Returns len, or -1. */
long wx_host_file_write(int fd, const void *buf, unsigned long len);

/** Close a descriptor. Returns 0 or -1. */
int wx_host_file_close(int fd);

/** Host monotonic clock in microseconds: wall time, for speed checks. */
long long wx_host_monotonic_us(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKES_HOST_FILE_H_ */
