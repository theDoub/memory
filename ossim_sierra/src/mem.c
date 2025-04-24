#include "mem.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <os-mm.h>
#include <stdint.h>
#include <common.h>

#define MAX_VIRTUAL_MEMORY 65536 // Define MAX_VIRTUAL_MEMORY with an appropriate value

static BYTE _ram[RAM_SIZE];

static struct {
    uint32_t proc; // ID of process currently uses this page
    int index;     // Index of the page in the list of pages allocated
    int next;      // The next page in the list. -1 if it is the last
} _mem_stat[NUM_PAGES];

static pthread_mutex_t mem_lock;

void init_mem(void) {
    memset(_mem_stat, 0, sizeof(*_mem_stat) * NUM_PAGES);
    memset(_ram, 0, sizeof(BYTE) * RAM_SIZE);
    pthread_mutex_init(&mem_lock, NULL);
}

/* get offset of the virtual address */
static addr_t get_offset(addr_t addr) {
    return addr & ~((~0U) << OFFSET_LEN);
}

/* get the first layer index */
static addr_t get_first_lv(addr_t addr) {
    return addr >> (OFFSET_LEN + PAGE_LEN);
}

/* get the second layer index */
static addr_t get_second_lv(addr_t addr) {
    return (addr >> OFFSET_LEN) & ((1 << PAGE_LEN) - 1);
}

/* Search for page table table from the segment table */
static struct trans_table_t *get_trans_table(addr_t index, struct page_table_t *page_table) {
    for (int i = 0; i < page_table->size; i++) {
        if (page_table->table[i].v_index == index) {
            // Cast to correct type for compatibility
            return (struct trans_table_t *)&page_table->table[i];
        }
    }
    return NULL;
}

/* Translate virtual address to physical address. 
 * return 1 and write its physical counterpart to [physical_addr].
 * Otherwise, return 0 */
static int translate(
    addr_t virtual_addr, 	// Given virtual address
    addr_t * physical_addr, // Physical address to be returned
    struct pcb_t * proc) {  // Process uses given virtual address

/* Offset of the virtual address */
addr_t offset = get_offset(virtual_addr);
    offset++; offset--;
/* The first layer index */
addr_t first_lv = get_first_lv(virtual_addr);
/* The second layer index */
addr_t second_lv = get_second_lv(virtual_addr);

/* Search in the first level */
struct trans_table_t * trans_table = NULL;
trans_table = get_trans_table(first_lv, proc->page_table);
if (trans_table == NULL) {
    return 0;
}

int i;
for (i = 0; i < trans_table->size; i++) {
    if (trans_table->table[i].v_index == second_lv) {
        /* DO NOTHING HERE. This mem is obsoleted */
        return 1;
    }
}
return 0;	
}

addr_t alloc_mem(uint32_t size, struct pcb_t *proc) {
    pthread_mutex_lock(&mem_lock);
    addr_t ret_mem = 0;

    uint32_t num_pages = (size % PAGE_SIZE) ? size / PAGE_SIZE :
		size / PAGE_SIZE + 1; // Number of pages we will use
	int mem_avail = 0; // Check availability

    if (mem_avail) {
        ret_mem = proc->bp;
        proc->bp += num_pages * PAGE_SIZE;

        for (int i = 0; i < num_pages; i++) {
            for (int j = 0; j < NUM_PAGES; j++) {
                if (_mem_stat[j].proc == 0) {
                    _mem_stat[j].proc = proc->pid;
                    _mem_stat[j].index = i;
                    _mem_stat[j].next = -1; // Last page
                    // Update page table (simplified)
                    proc->page_table->table[j].v_index = ret_mem / PAGE_SIZE + i;
                    proc->page_table->table[j].v_index = j;
                    proc->page_table->size++;
                    break;
                }
            }
        }
    }

    pthread_mutex_unlock(&mem_lock);
    return ret_mem;
}

int free_mem(addr_t address, struct pcb_t * proc) {
	/* DO NOTHING HERE. This mem is obsoleted */
	return 0;
}

int read_mem(addr_t address, struct pcb_t *proc, BYTE *data) {
    addr_t physical_addr;
    if (translate(address, &physical_addr, proc)) {
        *data = _ram[physical_addr];
        return 0;
    } else {
        return 1; // Page not present
    }
}

int write_mem(addr_t address, struct pcb_t *proc, BYTE data) {
    addr_t physical_addr;
    if (translate(address, &physical_addr, proc)) {
        _ram[physical_addr] = data;
        return 0;
    } else {
        return 1; // Page not present
    }
}

void dump(void) {
	int i;
	for (i = 0; i < NUM_PAGES; i++) {
		if (_mem_stat[i].proc != 0) {
			printf("%03d: ", i);
			printf("%05x-%05x - PID: %02d (idx %03d, nxt: %03d)\n",
				i << OFFSET_LEN,
				((i + 1) << OFFSET_LEN) - 1,
				_mem_stat[i].proc,
				_mem_stat[i].index,
				_mem_stat[i].next
			);
			int j;
			for (	j = i << OFFSET_LEN;
				j < ((i+1) << OFFSET_LEN) - 1;
				j++) {
				
				if (_ram[j] != 0) {
					printf("\t%05x: %02x\n", j, _ram[j]);
				}
					
			}
		}
	}
}