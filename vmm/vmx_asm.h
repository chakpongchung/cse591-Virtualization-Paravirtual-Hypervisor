#ifndef JOS_VMM_ASM_H
#define JOS_VMM_ASM_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/types.h>

#define ASM_VMX_VMCLEAR_RAX       ".byte 0x66, 0x0f, 0xc7, 0x30"
#define ASM_VMX_VMLAUNCH          ".byte 0x0f, 0x01, 0xc2"
#define ASM_VMX_VMRESUME          ".byte 0x0f, 0x01, 0xc3"
#define ASM_VMX_VMPTRLD_RAX       ".byte 0x0f, 0xc7, 0x30"
#define ASM_VMX_VMREAD_RDX_RAX    ".byte 0x0f, 0x78, 0xd0"
#define ASM_VMX_VMWRITE_RAX_RDX   ".byte 0x0f, 0x79, 0xd0"
#define ASM_VMX_VMWRITE_RSP_RDX   ".byte 0x0f, 0x79, 0xd4"
#define ASM_VMX_VMXOFF            ".byte 0x0f, 0x01, 0xc4"
#define ASM_VMX_VMXON_RAX         ".byte 0xf3, 0x0f, 0xc7, 0x30"
#define ASM_VMX_INVEPT            ".byte 0x66, 0x0f, 0x38, 0x80, 0x08"
#define ASM_VMX_INVVPID           ".byte 0x66, 0x0f, 0x38, 0x81, 0x08"

static __inline uint8_t vmxon( physaddr_t vmxon_region ) __attribute((always_inline));
static __inline uint8_t vmclear( physaddr_t vmcs_region ) __attribute((always_inline));
static __inline uint8_t vmptrld( physaddr_t vmcs_region ) __attribute((always_inline));


static __inline uint8_t
vmxon( physaddr_t vmxon_region ) {
	uint8_t error = 0;

    __asm __volatile("clc; vmxon %1; setna %0"
            : "=q"( error ) : "m" ( vmxon_region ): "cc" );
    return error;
}

static __inline uint8_t
vmclear( physaddr_t vmcs_region ) {
	uint8_t error = 0;

    __asm __volatile("clc; vmclear %1; setna %0"
            : "=q"( error ) : "m" ( vmcs_region ) : "cc");
    return error;
}

static __inline uint8_t
vmptrld( physaddr_t vmcs_region ) {
	uint8_t error = 0;

    __asm __volatile("clc; vmptrld %1; setna %0"
            : "=q"( error ) : "m" ( vmcs_region ) : "cc");
    return error;
}

static __inline uint8_t
vmlaunch() {
	uint8_t error = 0;

    __asm __volatile("clc; vmlaunch; setna %0"
            : "=q"( error ) :: "cc");
    return error;
}

#endif

