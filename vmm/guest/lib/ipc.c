// User-level IPC library routines

#include <inc/lib.h>
#ifdef VMM_GUEST
#include <inc/vmx.h>
#endif

// Receive a value via IPC and return it.
// If 'pg' is nonnull, then any page sent by the sender will be mapped at
//	that address.
// If 'from_env_store' is nonnull, then store the IPC sender's envid in
//	*from_env_store.
// If 'perm_store' is nonnull, then store the IPC sender's page permission
//	in *perm_store (this is nonzero iff a page was successfully
//	transferred to 'pg').
// If the system call fails, then store 0 in *fromenv and *perm (if
//	they're nonnull) and return the error.
// Otherwise, return the value sent by the sender
//
// Hint:
//   Use 'thisenv' to discover the value and who sent it.
//   If 'pg' is null, pass sys_ipc_recv a value that it will understand
//   as meaning "no page".  (Zero is not the right value, since that's
//   a perfectly valid place to map a page.)

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
if (num != 5) 
{
	if(ret > 0)
          panic("vm_call %d returned %d (> 0)", num, ret);
}
           return ret; 

}

    int32_t
ipc_recv(envid_t *from_env_store, void *pg, int *perm_store)
{
	// LAB 4: Your code here.

	//If pg is NULL, then set pg to something that sys_ipc_rev can decode
	if (pg == NULL)
		pg=(void*)UTOP;

	//Try receiving value
	int r = sys_ipc_recv(pg);
	if (r < 0) {
		if (from_env_store)
			*from_env_store = 0;
		if (perm_store)
			*perm_store = 0;
		return r;
	}
	else {
		if (from_env_store != NULL)
                	*from_env_store = thisenv->env_ipc_from;
        	if (thisenv->env_ipc_dstva && perm_store != NULL)
                	*perm_store = thisenv->env_ipc_perm;

		return thisenv->env_ipc_value; //return the received value
	}

	panic("ipc_recv not implemented");
}

// Send 'val' (and 'pg' with 'perm', if 'pg' is nonnull) to 'toenv'.
// This function keeps trying until it succeeds.
// It should panic() on any error other than -E_IPC_NOT_RECV.
//
// Hint:
//   Use sys_yield() to be CPU-friendly.
//   If 'pg' is null, pass sys_ipc_recv a value that it will understand
//   as meaning "no page".  (Zero is not the right value.)
    void
ipc_send(envid_t to_env, uint32_t val, void *pg, int perm)
{
    // LAB 4: Your code here.
    //If pg is NULL, then set pg to something that sys_ipc_rev can decode
        if (pg == NULL)
                pg=(void*)UTOP;
    /*uint64_t addr = (uint64_t)pg;                                                                                                                                   
    
    if( (vpml4e[VPML4E(addr)] & PTE_P)   &&   (vpde[VPDPE(addr)] & PTE_P)                                                                                     
                        &&  (vpd[VPD(addr)] & PTE_P)  &&  (vpt[VPN(addr)] & PTE_P)  )
    {
           pg = (void *) PTE_ADDR( vpt[VPN(addr)] );
    }*/
//    cprintf("Trying to send to : %d,", to_env);
	//Loop until succeeded/
	while (1) {
		//Try sending the value to dst
		int r = sys_ipc_try_send(to_env, val, pg, perm);

		if (r == 0)
			break;
		if (r < 0 && r != -E_IPC_NOT_RECV) //Receiver is not ready to receive.
			panic("error in sys_ipc_try_send %e\n", r);
		else if (r == -E_IPC_NOT_RECV) 
			sys_yield();
	}
}

#ifdef VMM_GUEST

// Access to host IPC interface through VMCALL.
// Should behave similarly to ipc_recv, except replacing the system call with a vmcall.
int32_t
ipc_host_recv(void *pg) {
    // LAB 8: Your code here.

    if (pg == NULL)
        pg=(void*)UTOP;
   
    uint64_t addr = (uint64_t)pg;                                                                                                                                   
    
    if( (vpml4e[VPML4E(addr)] & PTE_P)   &&   (vpde[VPDPE(addr)] & PTE_P)                                                                                     
                        &&  (vpd[VPD(addr)] & PTE_P)  &&  (vpt[VPN(addr)] & PTE_P)  )
    {
           pg = (void *) PTE_ADDR( vpt[VPN(addr)] );
    }

	//Try receiving value
	//int r = sys_ipc_recv(pg);
//        cprintf("sending recv to host from guest");
	int r = vm_call( VMX_VMCALL_IPCRECV, (uint64_t) pg , 0, 0, 0, 0 );
/*	if (r < 0) {
		if (from_env_store)
			*from_env_store = 0;
		if (perm_store)
			*perm_store = 0;
		return r;
	}
	else {
		if (from_env_store != NULL)
                	*from_env_store = thisenv->env_ipc_from;
        	if (thisenv->env_ipc_dstva && perm_store != NULL)
                	*perm_store = thisenv->env_ipc_perm;

		return thisenv->env_ipc_value; //return the received value
	}

	panic("ipc_recv not implemented");
*/  
//cprintf("ipc_recv implemented in VM guest");
//    curenv->env_status = ENV_NOT_RUNNABLE;
    return r;
}

// Access to host IPC interface through VMCALL.
// Should behave similarly to ipc_send, except replacing the system call with a vmcall.
void
ipc_host_send(envid_t to_env, uint32_t val, void *pg, int perm)
{
    // LAB 8: Your code here.
    uint64_t addr = (uint64_t)pg;
	if (pg == NULL)
                pg=(void*)UTOP;

    
        if( (vpml4e[VPML4E(addr)] & PTE_P)   &&   (vpde[VPDPE(addr)] & PTE_P)                                                                                     
                        &&  (vpd[VPD(addr)] & PTE_P)  &&  (vpt[VPN(addr)] & PTE_P)  )
        {

                pg = (void *) PTE_ADDR( vpt[VPN(addr)] );

        }

	//Loop until succeeded/
	while (1) {
		//Try sending the value to dst
		//int r = sys_ipc_try_send(to_env, val, pg, perm);

		int r = vm_call( VMX_VMCALL_IPCSEND, (uint64_t) (to_env) , (uint64_t) val, (uint64_t) pg, (uint64_t) perm, 0 );
                
		if (r == 0)
		{
//                    cprintf("Host sending message..... \n");
                    break;
                }
		if (r < 0 && r != -E_IPC_NOT_RECV) //Receiver is not ready to receive.
			panic("error in sys_ipc_try_send %e\n", r);
		else if (r == -E_IPC_NOT_RECV) 
			sys_yield();
	}
//    panic("ipc_send not implemented in VM guest");
}

#endif

// Find the first environment of the given type.  We'll use this to
// find special environments.
// Returns 0 if no such environment exists.
    envid_t
ipc_find_env(enum EnvType type)
{
    int i;
    for (i = 0; i < NENV; i++) {
        if (envs[i].env_type == type)
            return envs[i].env_id;
    }
    return 0;
}
