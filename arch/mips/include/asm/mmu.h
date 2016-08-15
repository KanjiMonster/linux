#ifndef __ASM_MMU_H
#define __ASM_MMU_H

#include <linux/atomic.h>
#include <linux/vmalloc.h>

typedef struct {
	unsigned long asid[NR_CPUS];
	void *vdso;
	atomic_t fp_mode_switching;
} mm_context_t;

struct static_vm {
	struct vm_struct vm;
	struct list_head list;
};

extern void __init add_static_vm_early(struct static_vm *svm);

static inline void add_identity_vm_early(struct static_vm *svm,
					        unsigned long addr,
						unsigned long size,
						void *caller)
{
	struct vm_struct *vm = &svm->vm;

	vm->addr = (void *)addr;
	vm->size = size;
	vm->phys_addr = addr;
	vm->flags = VM_IOREMAP;
	vm->caller = caller;

	add_static_vm_early(svm);
}
#endif /* __ASM_MMU_H */
