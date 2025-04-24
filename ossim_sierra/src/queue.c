#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t * q) {
        if (q == NULL) return 1;
	return (q->size == 0);
}

void enqueue(struct queue_t * q, struct pcb_t * proc) {
        /* TODO: put a new process to queue [q] */
        if (q == NULL || proc == NULL) return;
        if (q->size >= MAX_QUEUE_SIZE) {
                fprintf(stderr, "Queue is full, cannot enqueue process %d\n", proc->pid);
                return;
        }

        // Safety check for size bounds
        if (q->size < 0)
                q->size = 0;

        // Add the process to the end of the queue
        q->proc[q->size] = proc;
        q->size++;
}

struct pcb_t * dequeue(struct queue_t * q) {
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * */
        if (q == NULL || q->size <= 0) return NULL;
        
        // Validate the queue size
        if (q->size > MAX_QUEUE_SIZE) {
                q->size = MAX_QUEUE_SIZE;
        }
        
        // Find the process with highest priority (lowest number)
        int highest_priority_idx = 0;
        for (int i = 1; i < q->size; i++) {
                if (q->proc[i] == NULL || q->proc[highest_priority_idx] == NULL) {
                        continue; // Skip null entries for safety
                }
#ifdef MLQ_SCHED
                if (q->proc[i]->prio < q->proc[highest_priority_idx]->prio) {
                        highest_priority_idx = i;
                }
#else
                if (q->proc[i]->priority < q->proc[highest_priority_idx]->priority) {
                        highest_priority_idx = i;
                }
#endif
        }
        
        // Safety check
        if (highest_priority_idx >= q->size || q->proc[highest_priority_idx] == NULL) {
                return NULL;
        }
        
        // Save the highest priority process
        struct pcb_t * highest_proc = q->proc[highest_priority_idx];
        
        // Remove the process from the queue and shift other processes
        for (int i = highest_priority_idx; i < q->size - 1; i++) {
                q->proc[i] = q->proc[i + 1];
        }
        
        q->size--;
        return highest_proc;
}

