/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

 #include "string.h"
 #include "mm.h"
 #include "syscall.h"
 #include "libmem.h"
 #include <stdlib.h>
 
 /* Define PAGING_ADDR_SHIFT */
 #ifndef PAGING_ADDR_SHIFT
 #define PAGING_ADDR_SHIFT 12 
 #endif
 #include <stdio.h>
 #include <pthread.h>
 
 static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;
 
 /* enlist_vm_freerg_list - add new rg to freerg_list */
 int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
 {
   struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;
 
   if (rg_elmt->rg_start >= rg_elmt->rg_end)
     return -1;
 
   if (rg_node != NULL)
     rg_elmt->rg_next = rg_node;
 
   mm->mmap->vm_freerg_list = rg_elmt;
 
   return 0;
 }
 
 /* get_symrg_byid - get mem region by region ID */
 struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
 {
   if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
     return NULL;
 
   return &mm->symrgtbl[rgid];
 }
 
 /* __alloc - allocate a region memory */
 int __alloc(struct pcb_t *caller, int vmaid, int rgid, int size, int *alloc_addr)
 {
   struct vm_rg_struct rgnode;
 
   if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
   {
     caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
     caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
     *alloc_addr = rgnode.rg_start;
 
     pthread_mutex_unlock(&mmvm_lock);
     return 0;
   }
 
   struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
   int inc_sz = PAGING_PAGE_ALIGNSZ(size);
   int inc_limit_ret;
 
   if (cur_vma == NULL)
     return -1;
 
   inc_limit_ret = inc_vma_limit(caller, vmaid, inc_sz);
   if (inc_limit_ret < 0)
     return -1;
 
   if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
   {
     caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
     caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
     *alloc_addr = rgnode.rg_start;
 
     pthread_mutex_unlock(&mmvm_lock);
     return 0;
   }
 
   return -1;
 }
 
 /* __free - remove a region memory */
 int __free(struct pcb_t *caller, int vmaid, int rgid)
 {
   if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
     return -1;
 
   struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);
   if (rgnode == NULL)
     return -1;
 
   enlist_vm_freerg_list(caller->mm, rgnode);
 
   return 0;
 }
 
 /* liballoc - PAGING-based allocate a region memory */
 int liballoc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
 {
   int addr;
   return __alloc(proc, 0, reg_index, size, &addr);
 }
 
 /* libfree - PAGING-based free a region memory */
 int libfree(struct pcb_t *proc, uint32_t reg_index)
 {
   return __free(proc, 0, reg_index);
 }
 
 /* pg_getpage - get the page in RAM */
 int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
 {
   uint32_t pte = mm->pgd[pgn];
 
   if (!PAGING_PAGE_PRESENT(pte))
   {
     int vicpgn, swpfpn, tgtfpn;
 
     find_victim_page(caller->mm, &vicpgn);
     MEMPHY_get_freefp(caller->active_mswp, &swpfpn);
 
     tgtfpn = PAGING_PTE_SWP(pte);
 
     __mm_swap_page(caller, vicpgn, swpfpn);
     __mm_swap_page(caller, tgtfpn, vicpgn);
 
     pte_set_swap(&mm->pgd[vicpgn], 0, swpfpn);
     pte_set_fpn(&mm->pgd[pgn], vicpgn);
 
     enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
   }
 
   *fpn = PAGING_FPN(mm->pgd[pgn]);
   return 0;
 }
 
 /* pg_getval - read value at given offset */
 int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
 {
   int pgn = PAGING_PGN(addr);
   int off = PAGING_OFFST(addr);
   int fpn;
 
   if (pg_getpage(mm, pgn, &fpn, caller) != 0)
     return -1;
 
   int phyaddr = (fpn << PAGING_ADDR_SHIFT) | off;
   MEMPHY_read(caller->mram, phyaddr, data);
 
   return 0;
 }
 
 /* pg_setval - write value to given offset */
 int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
 {
   int pgn = PAGING_PGN(addr);
   int off = PAGING_OFFST(addr);
   int fpn;
 
   if (pg_getpage(mm, pgn, &fpn, caller) != 0)
     return -1;
 
   int phyaddr = (fpn << PAGING_ADDR_SHIFT) | off;
   MEMPHY_write(caller->mram, phyaddr, value);
 
   return 0;
 }
 
 /*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE *data)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
    return -1;

  pg_getval(caller->mm, currg->rg_start + offset, data, caller);

  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    uint32_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);

  /* TODO update result of reading action*/
  //destination 
#ifdef IODUMP
  printf("read region=%d offset=%d value=%d\n", source, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  MEMPHY_dump(proc->mram);
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE value)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
    return -1;

  pg_setval(caller->mm, currg->rg_start + offset, value, caller);

  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    uint32_t offset)
{
#ifdef IODUMP
  printf("write region=%d offset=%d value=%d\n", destination, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  MEMPHY_dump(proc->mram);
#endif

  return __write(proc, 0, destination, offset, data);
}

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  int pagenum, fpn;
  uint32_t pte;


  for(pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte= caller->mm->pgd[pagenum];

    if (!PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_PTE_FPN(pte);
      MEMPHY_put_freefp(caller->mram, fpn);
    } else {
      fpn = PAGING_PTE_SWP(pte);
      MEMPHY_put_freefp(caller->active_mswp, fpn);    
    }
  }

  return 0;
}

 /* find_victim_page - find victim page */
 int find_victim_page(struct mm_struct *mm, int *retpgn)
 {
   struct pgn_t *pg = mm->fifo_pgn;
 
   if (pg == NULL)
     return -1;
 
   *retpgn = pg->pgn;
   mm->fifo_pgn = pg->pg_next;
 
   free(pg);
   return 0;
 }
 
 /* get_free_vmrg_area - get a free vm region */
 int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
 {
   struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
   struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;
 
   while (rgit != NULL)
   {
     if ((rgit->rg_end - rgit->rg_start) >= size)
     {
       newrg->rg_start = rgit->rg_start;
       newrg->rg_end = rgit->rg_start + size;
 
       rgit->rg_start += size;
       if (rgit->rg_start == rgit->rg_end)
       {
         cur_vma->vm_freerg_list = rgit->rg_next;
         free(rgit);
       }
 
       return 0;
     }
 
     rgit = rgit->rg_next;
   }
 
   return -1;
 }

int print_pgtbl(struct pcb_t *proc, uint32_t start, uint32_t end)
{
    if (proc == NULL || proc->mm == NULL) return -1;

    if (end < 0 || end > PAGING_MAX_PGN) end = PAGING_MAX_PGN;

    printf("=== Page Table Dump ===\n");
    for (uint32_t i = start; i < end; i++) {
        uint32_t pte = proc->mm->pgd[i];
        if (pte != 0) {
            printf("PTE[%u]: 0x%08x | present=%d | fpn=%d | swapped=%d | swp_offset=%d\n",
                   i,
                   pte,
                   (pte & PAGING_PTE_PRESENT_MASK) != 0,
                   PAGING_FPN(pte),
                   (pte & PAGING_PTE_SWAPPED_MASK) != 0,
                   PAGING_PTE_SWP(pte));
        }
    }
    return 0;
}