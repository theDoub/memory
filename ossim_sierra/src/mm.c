// #ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

 #include "mm.h"
 #include <stdlib.h>
 #include <stdio.h>
 #include <string.h> // Thêm string.h để khai báo memset và các hàm xử lý chuỗi
 
 /*
  * init_pte - Initialize PTE entry
  */

 int init_pte(uint32_t *pte,
              int pre,    // present
              int fpn,    // FPN
              int drt,    // dirty
              int swp,    // swap
              int swptyp, // swap type
              int swpoff) // swap offset
 {
     if (pre != 0) {
         if (swp == 0) { // Non swap ~ page online
             if (fpn == 0)
                 return -1; // Invalid setting
 
             /* Valid setting with FPN */
             SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
             CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
             CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);
 
             SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
         } else { // page swapped
             SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
             SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
             CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);
 
             SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
             SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
         }
     }
 
     return 0;
 }

/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(uint32_t *pte, int swptyp, int swpoff)
{
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/*
 * pte_set_swap - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(uint32_t *pte, int fpn)
{
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}
 /*
  * alloc_pages_range - allocate req_pgnum of frame in ram
  * @caller    : caller
  * @req_pgnum : request page num
  * @frm_lst   : frame list
  */
 int alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
 {
     int pgit, fpn;
     struct framephy_struct *newfp_str = NULL;
 
     for (pgit = 0; pgit < req_pgnum; pgit++) {
         if (MEMPHY_get_freefp(caller->mram, &fpn) == 0) { // Get a free frame
             newfp_str = malloc(sizeof(struct framephy_struct));
             if (newfp_str == NULL) { 
                 fprintf(stderr, "Error: Memory allocation failed\n");
                 return -1;
             }
             newfp_str->fpn = fpn;
             newfp_str->fp_next = *frm_lst;
             *frm_lst = newfp_str; // Add to the frame list
         } else {
             return -1; // Error: Not enough frames available
         }
     }
 
     return 0;
 }
 
 /*
  * vmap_page_range - map a range of page at aligned address
  */
 int vmap_page_range(struct pcb_t *caller,           // process call
                     int addr,                       // start address which is aligned to pagesz
                     int pgnum,                      // num of mapping page
                     struct framephy_struct *frames, // list of the mapped frames
                     struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp
 {
     struct framephy_struct *fpit = frames;
     int pgit = 0;
     int pgn = PAGING_PGN(addr);
 
  
     ret_rg->rg_start = addr;
     ret_rg->rg_end = addr + pgnum * PAGING_PAGESZ;
 

     while (pgit < pgnum && fpit != NULL) {
         uint32_t *pte = &caller->mm->pgd[pgn + pgit];
         pte_set_fpn(pte, fpit->fpn); // Set the frame page number in the page table
         fpit = fpit->fp_next;
         pgit++;
     }
 
   
     for (int i = 0; i < pgit; i++) {
         enlist_pgn_node(&caller->mm->fifo_pgn, pgn + i);
     }
 
     return 0;
 }
 
 /*
  * vm_map_ram - do the mapping all vm are to ram storage device
  */

 int vm_map_ram(struct pcb_t *caller, int astart, int aend, int mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
 {
     struct framephy_struct *frm_lst = NULL;
 
     /* Allocate frames for the requested pages */
     int ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);
     if (ret_alloc < 0) {
         return -1; // Out of memory
     }
 
     /* Map the allocated frames to the virtual address range */
     vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);
 
     return 0;
 }
 
/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, int srcfpn,
    struct memphy_struct *mpdst, int dstfpn)
{
int cellidx;
int addrsrc, addrdst;
for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
{
addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
addrdst = dstfpn * PAGING_PAGESZ + cellidx;

BYTE data;
MEMPHY_read(mpsrc, addrsrc, &data);
MEMPHY_write(mpdst, addrdst, data);
}

return 0;
}


 /*
  * Initialize a empty Memory Management instance
  * @mm:     self mm
  * @caller: mm owner
  */
 int init_mm(struct mm_struct *mm, struct pcb_t *caller)
 {
     struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
     if (vma0 == NULL) {
         fprintf(stderr, "Error: Memory allocation failed for VMA\n");
         return -1;
     }
 
     mm->pgd = malloc(PAGING_MAX_PGN * sizeof(uint32_t));
     if (mm->pgd == NULL) {
         fprintf(stderr, "Error: Memory allocation failed for page table\n");
         free(vma0);
         return -1;
     }
 
     memset(mm->pgd, 0, PAGING_MAX_PGN * sizeof(uint32_t)); // Initialize page table
 
   
     vma0->vm_id = 0;
     vma0->vm_start = 0;
     vma0->vm_end = vma0->vm_start;
     vma0->sbrk = vma0->vm_start;
 
     struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
     enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);
 
     vma0->vm_mm = mm;
     vma0->vm_next = NULL;
 
     mm->mmap = vma0;  
     mm->fifo_pgn = NULL; 
 
     return 0;
 }
 #include <stdlib.h>

/* Create a new vm_rg_struct with start and end addresses */
struct vm_rg_struct *init_vm_rg(int rg_start, int rg_end) {
    struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
    if (newrg == NULL) return NULL;

    newrg->rg_start = rg_start;
    newrg->rg_end = rg_end;
    newrg->rg_next = NULL;
    return newrg;
}

/* Enlist a vm_rg_struct node into a linked list of regions */
int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode) {
    if (rgnode == NULL) return -1;

    rgnode->rg_next = *rglist;
    *rglist = rgnode;
    return 0;
}

/* Enlist a page number node into the FIFO page list */
int enlist_pgn_node(struct pgn_t **pgnlist, int pgn) {
    struct pgn_t *newpgn = malloc(sizeof(struct pgn_t));
    if (newpgn == NULL) return -1;

    newpgn->pgn = pgn;
    newpgn->pg_next = *pgnlist;
    *pgnlist = newpgn;
    return 0;
}