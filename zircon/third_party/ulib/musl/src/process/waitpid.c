#include <errno.h>
#include <sys/wait.h>

pid_t waitpid(pid_t pid, int* status, int options) {
  // TODO(kulakowski) Actually wait on |pid|.
  errno = ENOSYS;
  return -1;
}
