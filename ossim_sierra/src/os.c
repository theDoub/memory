#include "cpu.h"
#include "timer.h"
#include "sched.h"
#include "loader.h"
#include "mm.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int time_slot;
static int num_cpus;
static int done = 0;

#ifdef MM_PAGING
static int memramsz;
static int memswpsz[PAGING_MAX_MMSWP];

struct mmpaging_ld_args {
	/* A dispatched argument struct to compact many-fields passing to loader */
	int vmemsz;
	struct memphy_struct *mram;
	struct memphy_struct **mswp;
	struct memphy_struct *active_mswp;
	int active_mswp_id;
	struct timer_id_t  *timer_id;
};
#endif

static struct ld_args{
	char ** path;
	unsigned long * start_time;
#ifdef MLQ_SCHED
	unsigned long * prio;
#endif
} ld_processes;
int num_processes;

struct cpu_args {
	struct timer_id_t * timer_id;
	int id;
};


static void * cpu_routine(void * args) {
	struct timer_id_t * timer_id = ((struct cpu_args*)args)->timer_id;
	int id = ((struct cpu_args*)args)->id;
	/* Check for new process in ready queue */
	int time_left = 0;
	struct pcb_t * proc = NULL;
	while (1) {
		/* Check the status of current process */
		if (proc == NULL) {
			/* No process is running, the we load new process from
	  		* ready queue */
			proc = get_proc();
			if (proc == NULL) {
                           next_slot(timer_id);
                           continue; /* First load failed. skip dummy load */
                        }
		}else if (proc->pc == proc->code->size) {
			/* The porcess has finish it job */
			printf("\tCPU %d: Processed %2d has finished\n",
				id ,proc->pid);
			free(proc);
			proc = get_proc();
			time_left = 0;
		}else if (time_left == 0) {
			/* The process has done its job in current time slot */
			printf("\tCPU %d: Put process %2d to run queue\n",
				id, proc->pid);
			put_proc(proc);
			proc = get_proc();
		}
		
		/* Recheck process status after loading new process */
		if (proc == NULL && done) {
			/* No process to run, exit */
			printf("\tCPU %d stopped\n", id);
			break;
		}else if (proc == NULL) {
			/* There may be new processes to run in
			 * next time slots, just skip current slot */
			next_slot(timer_id);
			continue;
		}else if (time_left == 0) {
			printf("\tCPU %d: Dispatched process %2d\n",
				id, proc->pid);
			time_left = time_slot;
		}
		
		/* Run current process */
		run(proc);
		time_left--;
		next_slot(timer_id);
	}
	detach_event(timer_id);
	pthread_exit(NULL);
}

static void * ld_routine(void * args) {
#ifdef MM_PAGING
	struct memphy_struct* mram = ((struct mmpaging_ld_args *)args)->mram;
	struct memphy_struct** mswp = ((struct mmpaging_ld_args *)args)->mswp;
	struct memphy_struct* active_mswp = ((struct mmpaging_ld_args *)args)->active_mswp;
	struct timer_id_t * timer_id = ((struct mmpaging_ld_args *)args)->timer_id;
#else
	struct timer_id_t * timer_id = (struct timer_id_t*)args;
#endif
	int i = 0;
	printf("ld_routine\n");
	while (i < num_processes) {
		struct pcb_t * proc = load(ld_processes.path[i]);
#ifdef MLQ_SCHED
		proc->prio = ld_processes.prio[i];
#endif
		while (current_time() < ld_processes.start_time[i]) {
			next_slot(timer_id);
		}
#ifdef MM_PAGING
		proc->mm = malloc(sizeof(struct mm_struct));
		init_mm(proc->mm, proc);
		proc->mram = mram;
		proc->mswp = mswp;
		proc->active_mswp = active_mswp;
#endif
		printf("\tLoaded a process at %s, PID: %d PRIO: %ld\n",
			ld_processes.path[i], proc->pid, ld_processes.prio[i]);
		add_proc(proc);
		free(ld_processes.path[i]);
		i++;
		next_slot(timer_id);
	}
	free(ld_processes.path);
	free(ld_processes.start_time);
	done = 1;
	detach_event(timer_id);
	pthread_exit(NULL);
}

static void read_config(const char * path) {
	FILE * file;
	if ((file = fopen(path, "r")) == NULL) {
		printf("Cannot find configure file at %s\n", path);
		exit(1);
	}

	char line[128];
	fgets(line, sizeof(line), file);
	sscanf(line, "%d %d %d", &time_slot, &num_cpus, &num_processes);

	ld_processes.path = (char**)malloc(sizeof(char*) * num_processes);
	ld_processes.start_time = (unsigned long*) malloc(sizeof(unsigned long) * num_processes);
#ifdef MLQ_SCHED
	ld_processes.prio = (unsigned long*) malloc(sizeof(unsigned long) * num_processes);
#endif

	long cursor = ftell(file);
	int temp[5];
	if (fgets(line, sizeof(line), file) && sscanf(line, "%d %d %d %d %d", &temp[0], &temp[1], &temp[2], &temp[3], &temp[4]) == 5) {
#ifdef MM_PAGING
		memramsz = temp[0];
		for (int i = 0; i < PAGING_MAX_MMSWP; i++) memswpsz[i] = temp[i+1];
#endif
	} else {
		fseek(file, cursor, SEEK_SET);
	}

	for (int i = 0; i < num_processes; i++) {
		char proc[100];
		ld_processes.path[i] = (char*)malloc(100);
		ld_processes.path[i][0] = '\0';
		strcat(ld_processes.path[i], "input/proc/");
		fgets(line, sizeof(line), file);
#ifdef MLQ_SCHED
		sscanf(line, "%lu %s %lu", &ld_processes.start_time[i], proc, &ld_processes.prio[i]);
#else
		sscanf(line, "%lu %s", &ld_processes.start_time[i], proc);
#endif
		strcat(ld_processes.path[i], proc);
	}
	fclose(file);
}

int main(int argc, char * argv[]) {
	if (argc != 2) {
		printf("Usage: os [path to configure file]\n");
		return 1;
	}

	char path[100] = "input/";
	strcat(path, argv[1]);
	read_config(path);

	pthread_t * cpu = (pthread_t*)malloc(num_cpus * sizeof(pthread_t));
	struct cpu_args * args = (struct cpu_args*)malloc(sizeof(struct cpu_args) * num_cpus);
	pthread_t ld;

	for (int i = 0; i < num_cpus; i++) {
		args[i].timer_id = attach_event();
		args[i].id = i;
	}
	struct timer_id_t * ld_event = attach_event();
	start_timer();

#ifdef MM_PAGING
	struct memphy_struct mram;
	struct memphy_struct mswp[PAGING_MAX_MMSWP];
	init_memphy(&mram, memramsz, 1);
	for (int i = 0; i < PAGING_MAX_MMSWP; i++) init_memphy(&mswp[i], memswpsz[i], 1);

	struct mmpaging_ld_args *mm_ld_args = malloc(sizeof(struct mmpaging_ld_args));
	mm_ld_args->timer_id = ld_event;
	mm_ld_args->mram = &mram;
	mm_ld_args->mswp = (struct memphy_struct **) &mswp;
	mm_ld_args->active_mswp = &mswp[0];
	mm_ld_args->active_mswp_id = 0;
	pthread_create(&ld, NULL, ld_routine, (void*)mm_ld_args);
#else
	pthread_create(&ld, NULL, ld_routine, (void*)ld_event);
#endif

	init_scheduler();
	for (int i = 0; i < num_cpus; i++) {
		pthread_create(&cpu[i], NULL, cpu_routine, (void*)&args[i]);
	}

	for (int i = 0; i < num_cpus; i++) {
		pthread_join(cpu[i], NULL);
	}
	pthread_join(ld, NULL);

	stop_timer();
	return 0;
}