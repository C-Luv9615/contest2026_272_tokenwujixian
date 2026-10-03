#ifndef HOSTSTUB_WQUEUE_H
#define HOSTSTUB_WQUEUE_H
#define HPWORK 1
#define LPWORK 2
struct work_s { void *arg; int queued; };
typedef void (*worker_t)(void *arg);
int work_queue(int qid, struct work_s *work, worker_t worker, void *arg,
               unsigned int delay);
int work_cancel(int qid, struct work_s *work);
#endif
