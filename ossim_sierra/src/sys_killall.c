/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "common.h"
#include "syscall.h"
#include "stdio.h"
#include "libmem.h"
#include "queue.h"
#include "string.h"

extern struct queue_t mlq_ready_queue[MAX_PRIO];

int __sys_killall(struct pcb_t *caller, struct sc_regs* regs)
{
    char proc_name[100];
    uint32_t data;

    //hardcode for demo only
    uint32_t memrg = regs->a1;
    
    /* TODO: Get name of the target proc */
    //proc_name = libread..
    int i = 0;
    data = 0;
    while(data != -1){
        libread(caller, memrg, i, &data);
        proc_name[i]= data;
        if(data == -1) proc_name[i]='\0';
        i++;
    }
    printf("The procname retrieved from memregionid %d is \"%s\"\n", memrg, proc_name);

    /* TODO: Traverse proclist to terminate the proc
     *       stcmp to check the process match proc_name
     */
    //caller->running_list
    //caller->mlq_ready_queu

    /* TODO Maching and terminating 
     *       all processes with given
     *        name in var proc_name
     */

    int killed = 0;

    for (int lvl = 0; lvl < MAX_PRIO; lvl++)
    {
        struct queue_t *queue = &mlq_ready_queue[lvl];
        int idx = 0;

        while (idx < queue->size)
        {
            struct pcb_t *proc = queue->proc[idx];
            if (strcmp(proc->path, proc_name) == 0)
            {
                proc->priority = -1;
                killed++;

                for (int k = idx; k < queue->size - 1; k++)
                {
                    queue->proc[k] = queue->proc[k + 1];
                }
                queue->size--;
            }
            else
            {
                idx++;
            }
        }
    }
    return killed;
}
