#include <inc/lib.h>
#include <inc/vmx.h>
#include <inc/elf.h>
#include <inc/ept.h>

#define GUEST_KERN "/vmm/kernel"
#define GUEST_BOOT "/vmm/boot"

#define JOS_ENTRY 0x7000
#define KERNEL_START 0x8000
// Map a region of file fd into the guest at guest physical address gpa.
// The file region to map should start at fileoffset and be length filesz.
// The region to map in the guest should be memsz.  The region can span multiple pages.
//
// Return 0 on success, <0 on failure.
//
static int
map_in_guest( envid_t guest, uintptr_t gpa, size_t memsz, 
        int fd, size_t filesz, off_t fileoffset ) {
    /* Your code here */
    return -E_NO_SYS;

} 

// Read the ELF headers of kernel file specified by fname,
// mapping all valid segments into guest physical memory as appropriate.
//
// Return 0 on success, <0 on error
//
// Hint: compare with ELF parsing in env.c, and use map_in_guest for each segment.
static int
copy_guest_kern_gpa( envid_t guest, char* fname ) {

    /* Your code here */
	struct Elf elf_struct;
        struct Elf *elf = &elf_struct;
        int fd = open(fname, O_RDONLY);
        uint64_t gpa = KERNEL_START;
        readn(fd, elf, sizeof(struct Elf));
	if (elf->e_magic != ELF_MAGIC)
		panic("Format of the binary is not correct\n");
        struct Proghdr ph_struct;
	struct Proghdr *ph, *eph;
	struct Page *p = NULL;
        
        ph = malloc(sizeof(struct Proghddr)*elf->e_phnum);
        readn(fd, ph, sizeof(struct Proghddr)*elf->e_phnum);
	ph = (struct Proghdr *) ((uint8_t *) elf + elf->e_phoff);
	eph = ph + elf->e_phnum;

	for (; ph < eph; ph++) {
		if (ph->p_type == ELF_PROG_LOAD) {
			uint64_t va = ph->p_va;
			uint64_t size = ph->p_memsz;
			uint64_t offset = ph->p_offset;
			uint64_t i = 0;
			if (ph->p_filesz > ph->p_memsz)
				panic("Wrong size in elf binary\n");
			map_in_guest(guest, gpa, ph->p_memsz, fd, ph->p_filesz, ph->p_offset);
                        
                        gpa = gpa + ph->p_memsz;
                        
                        seek(fd, 0);


			// Switch to env's address space so that we can use memcpy
			lcr3(e->env_cr3);
			memcpy((char*)ph->p_va, binary + ph->p_offset, ph->p_filesz);

			if (ph->p_filesz < ph->p_memsz)
				memset((char*)ph->p_va + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);

			// Switch back to kernel's address space
			lcr3(boot_cr3);
		}
	}

    // Now map one page for the program's initial stack
    // at virtual address USTACKTOP - PGSIZE.
	region_alloc(e, (void*)(USTACKTOP-PGSIZE), PGSIZE);

	// Magic to start executing environment at its address.
	e->env_tf.tf_rip = elf->e_entry;

    // LAB 3: Your code here.
   	e->elf = binary;

    return -E_NO_SYS;
}

void
umain(int argc, char **argv) {
    int ret;
    envid_t guest;

    if ((ret = sys_env_mkguest( GUEST_MEM_SZ, JOS_ENTRY )) < 0) {
        cprintf("Error creating a guest OS env: %e\n", ret );
        exit();
    }
    guest = ret;

    // Copy the guest kernel code into guest phys mem.
    if((ret = copy_guest_kern_gpa(guest, GUEST_KERN)) < 0) {
	cprintf("Error copying page into the guest - %d\n.", ret);
        exit();
    }

    // Now copy the bootloader.
    int fd;
    if ((fd = open( GUEST_BOOT, O_RDONLY)) < 0 ) {
        cprintf("open %s for read: %e\n", GUEST_BOOT, fd );
        exit();
    }

    // sizeof(bootloader) < 512.
    if ((ret = map_in_guest(guest, JOS_ENTRY, 512, fd, 512, 0)) < 0) {
	cprintf("Error mapping bootloader into the guest - %d\n.", ret);
	exit();
    }

    // Mark the guest as runnable.
    sys_env_set_status(guest, ENV_RUNNABLE);
    wait(guest);
}


