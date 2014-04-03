#include <vmm/ept.h>

#include <inc/error.h>
#include <inc/memlayout.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <kern/env.h>
// Return the physical address of an ept entry
static inline uintptr_t epte_addr(epte_t epte)
{
	return (epte & EPTE_ADDR);
}

// Return the host kernel virtual address of an ept entry
static inline uintptr_t epte_page_vaddr(epte_t epte)
{
	return (uintptr_t) KADDR(epte_addr(epte));
}

// Return the flags from an ept entry
static inline epte_t epte_flags(epte_t epte)
{
	return (epte & EPTE_FLAGS);
}

// Return true if an ept entry's mapping is present
static inline int epte_present(epte_t epte)
{
	return (epte & __EPTE_FULL) > 0;
}

// Find the final ept entry for a given guest physical address,
// creating any missing intermediate extended page tables if create is non-zero.
//
// If epte_out is non-NULL, store the found epte_t* at this address.
//
// Return 0 on success.  
// 
// Error values:
//    -E_INVAL if eptrt is NULL
//    -E_NO_ENT if create == 0 and the intermediate page table entries are missing.
//    -E_NO_MEM if allocation of intermediate page table entries fails
//
// Hint: Set the permissions of intermediate ept entries to __EPTE_FULL.
//       The hardware ANDs the permissions at each level, so removing a permission
//       bit at the last level entry is sufficient (and the bookkeeping is much simpler).
static int ept_lookup_gpa(epte_t* eptrt, void *gpa, 
			  int create, epte_t **epte_out) {
    /* Your code here */
    epte_t * epte;
    int ret;
    
    if(!eptrt)
	return -E_INVAL;
    else
	return ept_pml4e_walk(eptrt, gpa, create, epte_out);

    //panic("ept_lookup_gpa not implemented\n");
    //return 0;

}

int 
ept_pml4e_walk(epte_t *eptrt, const void *gpa, int create, epte_t **epte_out)
{
	uintptr_t index_in_ept_pml4t = PML4(gpa);
	epte_t *offsetd_ptr_in_ept_pml4t = eptrt + index_in_ept_pml4t;
	epte_t *pdpt_base = (epte_t*)(PTE_ADDR(*offsetd_ptr_in_ept_pml4t));
	int ret = 0;
	// Check if PDP does exists
	if (pdpt_base == NULL) {
		if (!create) return -E_NO_ENT;
		else {
			struct Page *newPage = page_alloc(ALLOC_ZERO);

			if (newPage == NULL) return -E_NO_MEM; // Out of memory

			newPage->pp_ref++;
			pdpt_base = (epte_t*)page2pa(newPage);
			ret = ept_pdpe_walk((epte_t*)page2kva(newPage), gpa, create, epte_out);

			if (ret < 0)
				page_decref(newPage); // Free allocated page for PDPE
			else {
				*offsetd_ptr_in_ept_pml4t = ((uint64_t)pdpt_base) | PTE_P | PTE_U | PTE_W;
			}
			return ret;
		}
	}
	else 
		return ept_pdpe_walk(KADDR((uint64_t)pdpt_base), gpa, create, epte_out); // PDP exists, so walk through it.

}

int ept_pdpe_walk(epte_t *pdpt_base,const void *gpa,int create, epte_t **epte_out)
{
        uintptr_t index_in_pdpt = PDPE(gpa);
        epte_t *offsetd_ptr_in_pdpt = pdpt_base + index_in_pdpt;
        epte_t *pgdir_base = (pde_t*) PTE_ADDR(*offsetd_ptr_in_pdpt);
        int ret = 0;
	//Check if PD exists
        if (pgdir_base == NULL)
        {
                if (create == 0) return E_NO_ENT;
                else {
                        struct Page *newPage = page_alloc(ALLOC_ZERO);

                        if (newPage == NULL) return E_NO_MEM;

                        newPage->pp_ref++;
                        pgdir_base = (epte_t*)page2pa(newPage);
                        ret = ept_pgdir_walk(page2kva(newPage), gpa, create, epte_out);

                        if (ret < 0) page_decref(newPage); // Free allocated page for PDE
                        else {
                                *offsetd_ptr_in_pdpt = ((uint64_t)pgdir_base) | PTE_P | PTE_U | PTE_W;
                                //*ept_out = offsetd_ptr_in_pdpt;
                        }
			return ret;
               }
        }
        else
                return ept_pgdir_walk(KADDR((uint64_t)pgdir_base), gpa, create, epte_out); // PD is present, so walk through it

}
// Given 'pgdir', a pointer to a page directory, pgdir_walk returns
// a pointer to the page table entry (PTE). 
// The programming logic and the hints are the same as pml4e_walk
// and pdpe_walk.

int 
ept_pgdir_walk(pde_t *pgdir_base, const void *gpa, int create, epte_t **epte_out)
{
        uintptr_t index_in_pgdir = PDX(gpa);
        epte_t *offsetd_ptr_in_pgdir = pgdir_base + index_in_pgdir;
        epte_t *page_table_base = (epte_t*)(PTE_ADDR(*offsetd_ptr_in_pgdir));

	//Check if PT exists
        if (page_table_base == NULL) {
                if (create == 0) return -E_NO_ENT;
                else {
                        struct Page *newPage = page_alloc(ALLOC_ZERO);

                        if (newPage == NULL) return -E_NO_MEM;

                        newPage->pp_ref++;
                        page_table_base = (epte_t*)page2pa(newPage);
						*offsetd_ptr_in_pgdir = ((uint64_t)page_table_base) | PTE_P | PTE_W | PTE_U;

			// Return PTE
		        uintptr_t index_in_page_table = PTX(gpa);
		        pte_t *offsetd_ptr_in_page_table = page_table_base + index_in_page_table;
				*epte_out = (epte_t*)KADDR((uint64_t)offsetd_ptr_in_page_table);
				return 0;
		}
        }
        else {
		// PT exists, so return PTE
	        uintptr_t index_in_page_table = PTX(gpa);
        	pte_t *offsetd_ptr_in_page_table = page_table_base + index_in_page_table;
			*epte_out = (epte_t*)KADDR((uint64_t)offsetd_ptr_in_page_table);
			return 0;
		}

}

void ept_gpa2hva(epte_t* eptrt, void *gpa, void **hva) {
    epte_t* pte;
    int ret = ept_lookup_gpa(eptrt, gpa, 0, &pte);
    if(ret < 0) {
        *hva = NULL;
    } else {
        if(!epte_present(*pte)) {
           *hva = NULL;
        } else {
           *hva = KADDR(epte_addr(*pte));
        }
    }
}

static void free_ept_level(epte_t* eptrt, int level) {
    epte_t* dir = eptrt;
    int i;

    for(i=0; i<NPTENTRIES; ++i) {
        if(level != 0) {
            if(epte_present(dir[i])) {
                physaddr_t pa = epte_addr(dir[i]);
                free_ept_level((epte_t*) KADDR(pa), level-1);
                // free the table.
                page_decref(pa2page(pa));
            }
        } else {
            // Last level, free the guest physical page.
            if(epte_present(dir[i])) {
                physaddr_t pa = epte_addr(dir[i]);
                page_decref(pa2page(pa));
            }
        }
    }
    return;
}

// Free the EPT table entries and the EPT tables.
// NOTE: Does not deallocate EPT PML4 page.
void free_guest_mem(epte_t* eptrt) {
    free_ept_level(eptrt, EPT_LEVELS - 1);
}

// Add Page pp to a guest's EPT at guest physical address gpa
//  with permission perm.  eptrt is the EPT root.
// 
// Return 0 on success, <0 on failure.
//
int ept_page_insert(epte_t* eptrt, struct Page* pp, void* gpa, int perm) {

    /* Your code here */
    panic("ept_page_insert not implemented\n");
    return 0;
}

// Map host virtual address hva to guest physical address gpa,
// with permissions perm.  eptrt is a pointer to the extended
// page table root.
//
// Return 0 on success.
// 
// If the mapping already exists and overwrite is set to 0,
//  return -E_INVAL.
// 
// Hint: use ept_lookup_gpa to create the intermediate 
//       ept levels, and return the final epte_t pointer.
int ept_map_hva2gpa(epte_t* eptrt, void* hva, void* gpa, int perm, 
        int overwrite) {

    /* Your code here */
    physaddr_t hpa = 0;
    pte_t *pte = NULL;
    int ret = 0;

//    struct Page* pp = pa2page(rcr3());

//    uint64_t pml4e = page2kva(pp);
//    struct Page *p = page_lookup(pml4e, hva, &pte);

//    if(pte == NULL)
//        return -E_INVAL;

/*    if(perm & PTE_W) 
        if(!(*pte & PTE_W))
            return -E_INVAL;
*/
    hpa = PADDR(hva);

    ret = ept_lookup_gpa(eptrt, gpa, 1, (epte_t**)&pte);
    if(!ret)
    {
        if(*pte)
        {
            if(overwrite)
            {
                *pte = (pte_t)hpa | perm;
                //p->pp_ref++;
                return 0;
            }
            else
                return -E_INVAL;
        }
        else
        {
                *pte = (pte_t)hpa | perm;
                //p->pp_ref++;
                return 0;
        }
    }
    return ret;
    //panic("ept_map_hva2gpa not implemented\n");

}

int ept_alloc_static(epte_t *eptrt, struct VmxGuestInfo *ginfo) {
    physaddr_t i;
    
    for(i=0x0; i < 0xA0000; i+=PGSIZE) {
        struct Page *p = page_alloc(0);
        p->pp_ref += 1;
        int r = ept_map_hva2gpa(eptrt, page2kva(p), (void *)i, __EPTE_FULL, 0);
    }

    for(i=0x100000; i < ginfo->phys_sz; i+=PGSIZE) {
        struct Page *p = page_alloc(0);
        p->pp_ref += 1;
        int r = ept_map_hva2gpa(eptrt, page2kva(p), (void *)i, __EPTE_FULL, 0);
    }
    return 0;
}

//#ifdef TEST_EPT_MAP
#include <kern/env.h>
#include <kern/syscall.h>
int _export_sys_ept_map(envid_t srcenvid, void *srcva,
	    envid_t guest, void* guest_pa, int perm);

int test_ept_map(void)
{
	struct Env *srcenv, *dstenv;
	struct Page *pp;
	epte_t *epte;
	int r;

	/* Initialize source env */
	if ((r = env_alloc(&srcenv, 0)) < 0)
		panic("Failed to allocate env (%d)\n", r);
	if (!(pp = page_alloc(ALLOC_ZERO)))
		panic("Failed to allocate page (%d)\n", r);
	if ((r = page_insert(srcenv->env_pml4e, pp, UTEMP, 0)) < 0)
		panic("Failed to insert page (%d)\n", r);
	curenv = srcenv;

	/* Check if sys_ept_map correctly verify the target env */
	if ((r = env_alloc(&dstenv, srcenv->env_id)) < 0)
		panic("Failed to allocate env (%d)\n", r);
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP, __EPTE_READ)) < 0)
		cprintf("EPT map to non-guest env failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on non-guest env.\n");

	/*env_destroy(dstenv);*/

	if ((r = env_guest_alloc(&dstenv, srcenv->env_id)) < 0)
		panic("Failed to allocate guest env (%d)\n", r);
	dstenv->env_vmxinfo.phys_sz = (uint64_t)UTEMP + PGSIZE;

	/* Check if sys_ept_map can verify srcva correctly */
	if ((r = _export_sys_ept_map(srcenv->env_id, (void *)UTOP, dstenv->env_id, UTEMP, __EPTE_READ)) < 0)
		cprintf("EPT map from above UTOP area failed as expected (%d).\n", r);
	else
		panic("sys_ept_map from above UTOP area success\n");
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP+1, dstenv->env_id, UTEMP, __EPTE_READ)) < 0)
		cprintf("EPT map from unaligned srcva failed as expected (%d).\n", r);
	else
		panic("sys_ept_map from unaligned srcva success\n");

	/* Check if sys_ept_map can verify guest_pa correctly */
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP + PGSIZE, __EPTE_READ)) < 0)
		cprintf("EPT map to out-of-boundary area failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on out-of-boundary area\n");
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP-1, __EPTE_READ)) < 0)
		cprintf("EPT map to unaligned guest_pa failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on unaligned guest_pa\n");
	
        /* Check if the sys_ept_map can verify the permission correctly */
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP, 0)) < 0)
		cprintf("EPT map with empty perm parameter failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on empty perm\n");
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP, __EPTE_WRITE)) < 0)
		cprintf("EPT map with write perm parameter failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on write perm\n");

	/* Check if the sys_ept_map can succeed on correct setup */
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP, __EPTE_READ)) < 0)
		panic("Failed to do sys_ept_map (%d)\n", r);
	else
		cprintf("sys_ept_map finished normally.\n");

	/* Check if the mapping is valid */
	if ((r = ept_lookup_gpa(dstenv->env_pml4e, UTEMP, 0, &epte)) < 0)
		panic("Failed on ept_lookup_gpa (%d)\n", r);
	if (page2pa(pp) != (epte_addr(*epte)))
		panic("EPT mapping address mismatching (%x vs %x).\n",
				page2pa(pp), epte_addr(*epte));
	else
		cprintf("EPT mapping address looks good: %x vs %x.\n",
				page2pa(pp), epte_addr(*epte));

	/* stop running after test, as this is just a test run. */
	panic("Cheers! sys_ept_map seems to work correctly.\n");

	return 0;
}
//#endif

