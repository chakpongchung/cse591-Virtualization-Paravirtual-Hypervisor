
#include <vmm/vmx.h>
#include <inc/error.h>
#include <vmm/vmexits.h>
#include <vmm/ept.h>
#include <inc/x86.h>
#include <kern/sched.h>
#include <inc/assert.h>
#include <kern/pmap.h>
#include <kern/console.h>
#include <kern/kclock.h>
#include <kern/multiboot.h>
#include <inc/string.h>
#include <kern/syscall.h>
#include <kern/env.h>
#include <inc/ept.h>
#include <kern/e1000.h>

bool
find_msr_in_region(uint32_t msr_idx, uintptr_t *area, int area_sz, struct vmx_msr_entry **msr_entry) {
    struct vmx_msr_entry *entry = (struct vmx_msr_entry *)area;
    int i;
    for(i=0; i<area_sz; ++i) {
        if(entry->msr_index == msr_idx) {
            *msr_entry = entry;
            return true;
        }
    }
    return false;
}

bool
handle_rdmsr(struct Trapframe *tf, struct VmxGuestInfo *ginfo) {
    uint64_t msr = tf->tf_regs.reg_rcx;
    if(msr == EFER_MSR) {
        // TODO: setup msr_bitmap to ignore EFER_MSR
        uint64_t val;
        struct vmx_msr_entry *entry;
        bool r = find_msr_in_region(msr, ginfo->msr_guest_area, ginfo->msr_count, &entry);
        assert(r);
        val = entry->msr_value;

        tf->tf_regs.reg_rdx = val << 32;
        tf->tf_regs.reg_rax = val & 0xFFFFFFFF;

        tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
        return true;
    }

    return false;
}

bool 
handle_wrmsr(struct Trapframe *tf, struct VmxGuestInfo *ginfo) {
    uint64_t msr = tf->tf_regs.reg_rcx;
    if(msr == EFER_MSR) {

        uint64_t cur_val, new_val;
        struct vmx_msr_entry *entry;
        bool r = 
            find_msr_in_region(msr, ginfo->msr_guest_area, ginfo->msr_count, &entry);
        assert(r);
        cur_val = entry->msr_value;

        new_val = (tf->tf_regs.reg_rdx << 32)|tf->tf_regs.reg_rax;
        if(BIT(cur_val, EFER_LME) == 0 && BIT(new_val, EFER_LME) == 1) {
            // Long mode enable.
            uint32_t entry_ctls = vmcs_read32( VMCS_32BIT_CONTROL_VMENTRY_CONTROLS );
            entry_ctls |= VMCS_VMENTRY_x64_GUEST;
            vmcs_write32( VMCS_32BIT_CONTROL_VMENTRY_CONTROLS, 
                    entry_ctls );

        }

        entry->msr_value = new_val;
        tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
        return true;
    }

    return false;
}

bool
handle_eptviolation(uint64_t *eptrt, struct VmxGuestInfo *ginfo) {
    uint64_t gpa = vmcs_read64(VMCS_64BIT_GUEST_PHYSICAL_ADDR);
    int r;
    if(gpa < 0xA0000 || (gpa >= 0x100000 && gpa < ginfo->phys_sz)) {
        // Allocate a new page to the guest.
        struct Page *p = page_alloc(0);
        if(!p)
            return false;
        p->pp_ref += 1;
        r = ept_map_hva2gpa(eptrt, 
                page2kva(p), (void *)ROUNDDOWN(gpa, PGSIZE), __EPTE_FULL, 0);
        assert(r >= 0);
        //         cprintf("EPT violation for gpa:%x mapped KVA:%x\n", gpa, page2kva(p));
        return true;
    } else if (gpa >= CGA_BUF && gpa < CGA_BUF + PGSIZE) {
        // FIXME: This give direct access to VGA MMIO region.
        r = ept_map_hva2gpa(eptrt, 
                (void *)(KERNBASE + CGA_BUF), (void *)CGA_BUF, __EPTE_FULL, 0);
        //         cprintf("EPT violation for gpa:%x mapped KVA:%x\n", gpa,(KERNBASE + CGA_BUF));
        assert(r >= 0);
        return true;
    } else if (gpa >= 0xF0000  && gpa < 0xF0000 + 0x10000) {
        r = ept_map_hva2gpa(eptrt, 
                (void *)(KERNBASE + gpa), (void *)gpa, __EPTE_FULL, 0);
        assert(r >= 0);
        return true;
    } else if (gpa >= 0xfee00000) {
        r = ept_map_hva2gpa(eptrt, 
                (void *)(KERNBASE + gpa), (void *)gpa, __EPTE_FULL, 0);
        assert(r >= 0);
        return true;
    } 

    return false;
}

bool
handle_ioinstr(struct Trapframe *tf, struct VmxGuestInfo *ginfo) {
    static int port_iortc;

    uint64_t qualification = vmcs_read64(VMCS_VMEXIT_QUALIFICATION);
    int port_number = (qualification >> 16) & 0xFFFF;
    bool is_in = BIT(qualification, 3);
    bool handled = false;

    // handle reading physical memory from the CMOS.
    if(port_number == IO_RTC) {
        if(!is_in) {
            port_iortc = tf->tf_regs.reg_rax;
            handled = true;
        }
    } else if (port_number == IO_RTC + 1) {
        if(is_in) {
            if(port_iortc == NVRAM_BASELO) {
                tf->tf_regs.reg_rax = 640 & 0xFF;
                handled = true;
            } else if (port_iortc == NVRAM_BASEHI) {
                tf->tf_regs.reg_rax = (640 >> 8) & 0xFF;
                handled = true;
            } else if (port_iortc == NVRAM_EXTLO) {
                tf->tf_regs.reg_rax = ((ginfo->phys_sz / 1024) - 1024) & 0xFF;
                handled = true;
            } else if (port_iortc == NVRAM_EXTHI) {
                tf->tf_regs.reg_rax = (((ginfo->phys_sz / 1024) - 1024) >> 8) & 0xFF;
                handled = true;
            }
        }

    }

    if(handled) {
        tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
        return true;
    } else {
        cprintf("%x %x\n", qualification, port_iortc);
        return false;    
    }
}

// Emulate a cpuid instruction.
// It is sufficient to issue the cpuid instruction here and collect the return value.
// You can store the output of the instruction in Trapframe tf,
//  but you should hide the presence of vmx from the guest if processor features are requested.
// 
// Return true if the exit is handled properly, false if the VM should be terminated.
//
// Finally, you need to increment the program counter in the trap frame.
// 
// Hint: The TA's solution does not hard-code the length of the cpuid instruction.
    bool
handle_cpuid(struct Trapframe *tf, struct VmxGuestInfo *ginfo)
{
    /* Your code here */
    uint32_t eax, ebx, ecx, edx;
    uint32_t junkeax = (uint32_t) tf->tf_regs.reg_rax;

    cpuid( junkeax, &eax, &ebx, &ecx, &edx );

    if(junkeax == 1)
        ecx = ecx & (~(32));

    tf->tf_regs.reg_rax =(uint64_t) eax;
    tf->tf_regs.reg_rbx =(uint64_t) ebx;
    tf->tf_regs.reg_rcx =(uint64_t) ecx;
    tf->tf_regs.reg_rdx =(uint64_t) edx;


    tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);

    //cprintf("Handle cpuid not implemented\n");

    return true;

}

// Handle vmcall traps from the guest.
// We currently support 3 traps: read the virtual e820 map, 
//   and use host-level IPC (send andrecv).
//
// Return true if the exit is handled properly, false if the VM should be terminated.
//
// Finally, you need to increment the program counter in the trap frame.
// 
// Hint: The TA's solution does not hard-code the length of the cpuid instruction.//

    bool
handle_vmcall(struct Trapframe *tf, struct VmxGuestInfo *gInfo, uint64_t *eptrt)
{
    bool handled = false;
    multiboot_info_t mbinfo;
    int perm, r, i, buf_len = 0;
    void *gpa_pg, *hva_pg, *srcva;
    uint64_t to_env, gpa ;
    uint32_t val;
    struct Page *page; 
    uintptr_t *hva;
    uint64_t value;
    int len_tmp = 0;
    char *pkt;

    // phys address of the multiboot map in the guest.
    uint64_t multiboot_map_addr = 0x6000;

    switch(tf->tf_regs.reg_rax) {
        case VMX_VMCALL_MBMAP:
            // Craft a multiboot (e820) memory map for the guest.
            //
            // Create three  memory mapping segments: 640k of low mem, the I/O hole (unusable), and 
            //   high memory (phys_size - 1024k).
            //
            // Once the map is ready, find the kernel virtual address of the guest page (if present),
            //   or allocate one and map it at the multiboot_map_addr (0x6000).
            // Copy the mbinfo and memory_map_t (segment descriptions) into the guest page, and return
            //   a pointer to this region in rbx (as a guest physical address).
            /* Your code here */


            page = page_alloc(ALLOC_ZERO);

            page->pp_ref++;
            //r = page_insert(curenv->env_pml4e, page, UTEMP, PTE_W | PTE_U );
            //

            hva = page2kva(page);

            memory_map_t mmap[3];

            mmap[0].size = 20;
            mmap[0].base_addr_low = 0x0;
            mmap[0].base_addr_high = 0x0;
            mmap[0].length_low = IOPHYSMEM;
            mmap[0].length_high = 0x0;
            mmap[0].type = MB_TYPE_USABLE;

            mmap[1].size = 20;
            mmap[1].base_addr_low = IOPHYSMEM;
            mmap[1].base_addr_high = 0x0;
            mmap[1].length_low = 0x60000;
            mmap[1].length_high = 0x0;
            mmap[1].type = MB_TYPE_RESERVED;

            mmap[2].size = 20;
            mmap[2].base_addr_low = EXTPHYSMEM;
            mmap[2].base_addr_high = 0x0;
            mmap[2].length_low = gInfo->phys_sz - EXTPHYSMEM;
            mmap[2].length_high = 0x0;
            mmap[2].type = MB_TYPE_USABLE;

            mbinfo.flags = MB_FLAG_MMAP;
            mbinfo.mmap_addr = 0x6000 + sizeof(mbinfo);
            mbinfo.mmap_length = sizeof(mmap);


            memcpy(hva, &mbinfo, sizeof(multiboot_info_t));
            memcpy((void*)((uint64_t)hva + sizeof(mbinfo)),(void *) mmap, sizeof(mmap));

            ept_map_hva2gpa(eptrt, hva, (void *)multiboot_map_addr, __EPTE_FULL, 1);

            //asm("movq %%rax, %%rbx \n\t");

            //cprintf("e820 map hypercall not implemented\n");	   

            tf->tf_regs.reg_rbx = 0x6000;
            handled = true;
            cprintf("vmcall handle complete");
            break;

        case VMX_VMCALL_IPCSEND:
            // Issue the sys_ipc_send call to the host.
            // 
            // If the requested environment is the HOST FS, this call should
            //  do this translation.
            /* Your code here */
            //sys_ipc_try_send(envid_t envid, uint64_t value, void *srcva, int perm)

            to_env  =   tf->tf_regs.reg_rdx;
            value   =   tf->tf_regs.reg_rcx;
            srcva   =   (void *)tf->tf_regs.reg_rbx;
            perm    =   tf->tf_regs.reg_rdi;

            if( to_env  == 1  && curenv->env_type == ENV_TYPE_GUEST )
            {
                for ( i = 0; i < NENV; i++) 
                {
                    if( envs[i].env_type == ENV_TYPE_FS ) {
                        to_env = (uint64_t)(envs[i].env_id);
                        break;
                    }
                }   
            }

            //while (1) {
            //Try sending the value to dst
            //sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
            r = syscall( SYS_ipc_try_send, (uint64_t)to_env, (uint64_t)value, (uint64_t)srcva, (uint64_t)perm, 0);
            //r = sys_ipc_try_send(to_env, value, srcva, perm);
            //int r = sys_ipc_try_send(to_env, val, pg, perm);
            /*
               if (r == 0)
               break;
               if (r < 0 && r != -E_IPC_NOT_RECV) //Receiver is not ready to receive.
               panic("error in sys_ipc_try_send %e\n", r);
               else if (r == -E_IPC_NOT_RECV) 
               sched_yield();
               }*/

            tf->tf_regs.reg_rax = (uint64_t)r;
            //cprintf("IPC send hypercall implemented\n");	    
            handled = true;
            break;
        
        case VMX_VMCALL_IPCRECV:
            // Issue the sys_ipc_recv call for the guest.
            // NB: because recv can call schedule, clobbering the VMCS, 
            // you should go ahead and increment rip before this call.
            /* Your code here */
            gpa_pg  = (void *)tf->tf_regs.reg_rdx;
            tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
            //tf->tf_rip+=3;

            r = syscall(SYS_ipc_recv, (uint64_t)gpa_pg, 0, 0, 0, 0);
            //r = sys_ipc_recv(gpa_pg);

            cprintf("IPC recv hypercall implemented\n");	    
            handled = true;
            break;

        case VMX_VMCALL_NETSEND:

            gpa  =   tf->tf_regs.reg_rdx;
            buf_len  =   tf->tf_regs.reg_rcx;

            ept_gpa2hva(eptrt, (void *) gpa, (void *) &hva);
            //cprintf("Data : %x  length = %d\n", gpa, buf_len);

            //r = syscall(SYS_net_try_send, (uint64_t) hva, (uint64_t)tf->tf_regs.reg_rcx, (uint64_t)0, (uint64_t)0,(uint64_t)0) ; 
            r = e1000_transmit((char *) hva, buf_len);

            //cprintf("Data : %s  length = %d\n", hva, buf_len);
            tf->tf_regs.reg_rax = (uint64_t)r;
            //cprintf("IPC send hypercall implemented\n");	    
            handled = true;
            break;


        case VMX_VMCALL_NETRECV:

            gpa  =   tf->tf_regs.reg_rdx;
            ept_gpa2hva(eptrt, (void *) gpa, (void *) &hva);
            //            r = syscall(SYS_net_try_send, (uint64_t) hva, (uint64_t)tf->tf_regs.reg_rcx, (uint64_t)0, (uint64_t)0,(uint64_t)0) ; 
            r = e1000_guest_receive((char *) hva, &len_tmp );
            pkt = (char *) hva;
            /*cprintf("\n Data Packet in VMexit e1000:\n[");
              for (i = 0; i<len_tmp; i++)
              {
              cprintf(":%u", pkt[i]);
              }
              cprintf("]\n");
              */
            //cprintf("PHANY:%d:%s \n", __LINE__, __FILE__);
            //cprintf("Data : %s  length = %d\n", hva, buf_len);
            if ( r  == 0 )
                tf->tf_regs.reg_rax = (uint64_t) len_tmp ;
            else
                tf->tf_regs.reg_rax = (uint64_t) 0x000 ;
            //cprintf("IPC send hypercall implemented\n");	    
            //cprintf("PHANY:%d:%s \n", __LINE__, __FILE__);
            handled = true;
            break;
    }

    if(handled) {
        /* Advance the program counter by the length of the vmcall instruction. 
         * 
         * Hint: The TA solution does not hard-code the length of the vmcall instruction.
         */
        /* Your code here */
        //cprintf("VMCALL handled \n");
        tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
        //tf->tf_rip+=3;
    }
    return handled;
}

