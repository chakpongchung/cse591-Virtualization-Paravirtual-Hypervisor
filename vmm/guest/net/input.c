#include "ns.h"
#define VMX_VMCALL_NETRECV 0x5

extern union Nsipc nsipcbuf;

int32_t net_host_recv(char *pg, int *len) {
    // LAB 8: Your code here.
    char * pkt ;
    int i;
    if ((pg == NULL) || ((uint64_t)pg >= UTOP))
        return -E_INVAL;    //pg = (void*) UTOP;

    uint64_t addr = (uint64_t)pg;                                                                                                                                   

    if( (vpml4e[VPML4E(addr)] & PTE_P)   &&   (vpde[VPDPE(addr)] & PTE_P)                                                                                     
            &&  (vpd[VPD(addr)] & PTE_P)  &&  (vpt[VPN(addr)] & PTE_P)  )
    {
        pg = (void *) PTE_ADDR( vpt[VPN(addr)] );
    }
//        cprintf("Data : %x  length = %d\n", pg, *len);

//    cprintf("PHANY:%d:%s \n", __LINE__, __FILE__);
    int ret =  vm_call( VMX_VMCALL_NETRECV, (uint64_t) pg , (uint64_t) *len, 0, 0, 0 );
//    cprintf("PHANY:%d:%s \n", __LINE__, __FILE__);
    pkt = (char *) addr;
//    cprintf("\n DATA IN GUEST INPUT.c e1000:\nlen [%d][", *len);
/*    for (i = 0; i<*len; i++)
    {
        cprintf(":%u", pkt[i]);
    }
    cprintf("]\n");
*/ 
    if (ret == 0)   {
        *len = 0;
        return -1;
    }
    else {
        *len = ret;
        return ret;
    }
}


void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	char *buf;
        buf = (char *)malloc(sizeof(char) * 2048);
        memset(buf, 0, 2048);

	int len, r, i;

	while (1) {
		// 	- read a packet from the device driver
		while ((r = net_host_recv(buf, &len)) < 0)
			sys_yield();

		// Whenever a new page is allocated, old will be deallocated by page_insert automatically.
		while ((r = sys_page_alloc(0, &nsipcbuf, PTE_U | PTE_P | PTE_W)) < 0);

		nsipcbuf.pkt.jp_len = len;
		memmove(nsipcbuf.pkt.jp_data, buf, len);

		while ((r = sys_ipc_try_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_P | PTE_W | PTE_U)) < 0);
	}

}
