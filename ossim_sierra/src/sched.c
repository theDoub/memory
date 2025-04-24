#include "queue.h"
#include "sched.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

// Sử dụng mutex để bảo vệ truy cập vào các hàng đợi
static pthread_mutex_t queue_lock;

static struct queue_t ready_queue;
static struct queue_t run_queue;
static struct queue_t running_list;

#ifdef MLQ_SCHED
struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];

// Các biến trạng thái dùng cho MLQ round-robin
// current_prio : chỉ số của queue hiện tại được xử lý (0 có ưu tiên cao nhất)
// current_slot_usage : số slot đã được dùng tại current_prio
static int current_prio = 0;
static int current_slot_usage = 0;
#endif

int queue_empty(void) {
#ifdef MLQ_SCHED
    unsigned long prio;
    for (prio = 0; prio < MAX_PRIO; prio++) {
        if (!empty(&mlq_ready_queue[prio]))
            return -1;
    }
#endif
    return (empty(&ready_queue) && empty(&run_queue));
}

void init_scheduler(void) {
#ifdef MLQ_SCHED
    int i;
    for (i = 0; i < MAX_PRIO; i++) {
        mlq_ready_queue[i].size = 0;
        // Số slot cấp cho mỗi mức ưu tiên: càng cao (prio nhỏ) thì slot càng nhiều
        slot[i] = MAX_PRIO - i;
    }
#endif
    ready_queue.size = 0;
    run_queue.size = 0;
    pthread_mutex_init(&queue_lock, NULL);
}

#ifdef MLQ_SCHED
/*
 * get_mlq_proc: Lấy một tiến trình từ mlq_ready_queue dựa trên chính sách MLQ với
 * round-robin theo slot. Nếu queue tại current_prio có tiến trình và chưa tiêu thụ
 * hết slot, tiến trình sẽ được lấy ra và current_slot_usage được tăng. Nếu hết slot,
 * chuyển sang mức ưu tiên tiếp theo.
 */
struct pcb_t * get_mlq_proc(void) {
    struct pcb_t * proc = NULL;
    
    pthread_mutex_lock(&queue_lock);

    // Bước 1: Kiểm tra xem có tiến trình nào ở mức ưu tiên cao hơn (prio nhỏ hơn)
    // so với current_prio hay không. Nếu có, chuyển current_prio về mức đó và reset slot.
    for (int pr = 0; pr < current_prio; pr++) {
        if (!empty(&mlq_ready_queue[pr])) {
            current_prio = pr;
            current_slot_usage = 0;
            break;
        }
    }
    
    int checked = 0;
    // Bước 2: Duyệt qua các hàng đợi theo thứ tự (vòng tròn) để tìm tiến trình
    while (checked < MAX_PRIO) {
        if (!empty(&mlq_ready_queue[current_prio])) {
            proc = dequeue(&mlq_ready_queue[current_prio]);
            current_slot_usage++; // tiêu thụ 1 slot
            
            // Nếu đã dùng đủ slot của hàng đợi hiện tại thì chuyển qua hàng đợi tiếp theo
            if (current_slot_usage >= slot[current_prio]) {
                current_prio = (current_prio + 1) % MAX_PRIO;
                current_slot_usage = 0;
            }
            break;
        } else {
            // Nếu hàng đợi hiện tại rỗng thì chuyển sang hàng đợi kế tiếp và reset slot
            current_prio = (current_prio + 1) % MAX_PRIO;
            current_slot_usage = 0;
        }
        checked++;
    }
    
    pthread_mutex_unlock(&queue_lock);
    return proc;
}

void put_mlq_proc(struct pcb_t * proc) {
    pthread_mutex_lock(&queue_lock);
    enqueue(&mlq_ready_queue[proc->prio], proc);
    pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t * proc) {
    pthread_mutex_lock(&queue_lock);
    enqueue(&mlq_ready_queue[proc->prio], proc);
    pthread_mutex_unlock(&queue_lock);
}

struct pcb_t * get_proc(void) {
    return get_mlq_proc();
}

void put_proc(struct pcb_t * proc) {
    if (proc == NULL) return;
    proc->ready_queue = &ready_queue;
    proc->mlq_ready_queue = mlq_ready_queue;
    proc->running_list = &running_list;
    
    // Lưu tiến trình vào danh sách running (cho mục đích bookkeeping)
    pthread_mutex_lock(&queue_lock);
    enqueue(&running_list, proc);
    pthread_mutex_unlock(&queue_lock);
    
    // Đưa tiến trình trở lại mlq_ready_queue sau 1 time-slice
    put_mlq_proc(proc);
}

void add_proc(struct pcb_t * proc) {
    if (proc == NULL) return;
    proc->ready_queue = &ready_queue;
    proc->mlq_ready_queue = mlq_ready_queue;
    proc->running_list = &running_list;
    
    // Lưu tiến trình vào danh sách running trước khi đưa vào mlq_ready_queue
    pthread_mutex_lock(&queue_lock);
    enqueue(&running_list, proc);
    pthread_mutex_unlock(&queue_lock);
    
    add_mlq_proc(proc);
}
#else
// Phần else cho chế độ scheduler không MLQ (không cần thay đổi)
struct pcb_t * get_proc(void) {
    struct pcb_t * proc = NULL;
    pthread_mutex_lock(&queue_lock);
    if (!empty(&ready_queue))
        proc = dequeue(&ready_queue);
    else if (!empty(&run_queue))
        proc = dequeue(&run_queue);
    pthread_mutex_unlock(&queue_lock);
    return proc;
}

void put_proc(struct pcb_t * proc) {
    proc->ready_queue = &ready_queue;
    proc->running_list = &running_list;

    /* TODO: put running proc to running_list */

    pthread_mutex_lock(&queue_lock);
    enqueue(&run_queue, proc);
    pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t * proc) {
    proc->ready_queue = &ready_queue;
    proc->running_list = &running_list;

    /* TODO: put running proc to running_list */

    pthread_mutex_lock(&queue_lock);
    enqueue(&ready_queue, proc);
    pthread_mutex_unlock(&queue_lock);    
}
#endif

