#ifndef _LINUX_FANOTIFY_H
#define _LINUX_FANOTIFY_H

#include <uapi/linux/fanotify.h>

#define FANOTIFY_INFO_MODES	(FANOTIFY_FID_BITS | FAN_REPORT_PIDFD)

#define FANOTIFY_ADMIN_INIT_FLAGS	(FANOTIFY_PERM_CLASSES | \
					 FAN_REPORT_TID | \
					 FAN_REPORT_PIDFD | \
					 FAN_UNLIMITED_QUEUE | \
					 FAN_UNLIMITED_MARKS)

/* not valid from userspace, only kernel internal */
#define FAN_MARK_ONDIR		0x00000100
#endif /* _LINUX_FANOTIFY_H */
