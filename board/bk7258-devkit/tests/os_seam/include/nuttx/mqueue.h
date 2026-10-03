#ifndef HOSTSTUB_MQUEUE_H
#define HOSTSTUB_MQUEUE_H
#include <stdint.h>
#include <stddef.h>
#include <nuttx/clock.h>
struct mq_attr { long mq_maxmsg; long mq_msgsize; int mq_flags; long mq_curmsgs; };
struct file
{
  struct mq_attr attr;
  int depth;
  int count;
  int item_size;
  unsigned char *buf;
};
int file_mq_open(struct file *mq, const char *name, int flags, int mode,
                 struct mq_attr *attr);
int file_mq_close(struct file *mq);
int file_mq_unlink(const char *name);
int file_mq_send(struct file *mq, const void *msg, size_t len, unsigned int prio);
int file_mq_ticksend(struct file *mq, const void *msg, size_t len,
                     unsigned int prio, sclock_t ticks);
ssize_t file_mq_receive(struct file *mq, void *msg, size_t len,
                        unsigned int *prio);
ssize_t file_mq_timedreceive(struct file *mq, void *msg, size_t len,
                             unsigned int *prio, sclock_t ticks);
int file_mq_getattr(struct file *mq, struct mq_attr *attr);
ssize_t file_mq_tickreceive(struct file *mq, void *msg, size_t len,
                            unsigned int *prio, sclock_t ticks);
#endif
