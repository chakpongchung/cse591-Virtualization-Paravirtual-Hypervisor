// Simple implementation of cprintf console output for the kernel,
// based on printfmt() and the kernel console's cputchar().

#include <inc/types.h>
#include <inc/stdio.h>
#include <inc/stdarg.h>
#include <inc/vmx.h>
#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <kern/pmap.h>
#include <kern/env.h>

int64_t 
vm_call(int num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    int ret = 0;

	asm volatile("vmcall\n"
             : "=a" (ret)
             : "a" (num),
               "d" (a1),
               "c" (a2),
               "b" (a3),
               "D" (a4),
               "S" (a5)
             : "cc", "memory");
  
	if(ret > 0)
         return ret;// panic("vm_call %d returned %d (> 0)", num, ret);
        else 
           return -1; 

}

static void
putch(int ch, int *cnt)
{
	cputchar(ch);
	*cnt++;
}

int
vcprintf(const char *fmt, va_list ap)
{

    pte_t *pte_fmt = pml4e_walk(curenv->env_pml4e, (void*)fmt, 0);
    pte_t *pte_ap = pml4e_walk(curenv->env_pml4e, (void*) &ap, 0);


    int r = vm_call( VMX_VMCALL_PRINTF, (uint64_t) PTE_ADDR(*pte_fmt), (uint64_t) PTE_ADDR(*pte_ap), 0, 0, 0 );

    return r;
    
	int cnt = 0;
    va_list aq;
    va_copy(aq,ap);
	vprintfmt((void*)putch, &cnt, fmt, aq);
    va_end(aq);
	return cnt;

}

int
cprintf(const char *fmt, ...)
{
	va_list ap;
	int cnt;
	va_start(ap, fmt);
    va_list aq;
    va_copy(aq,ap);
	cnt = vcprintf(fmt, aq);
	va_end(aq);

	return cnt;
}

