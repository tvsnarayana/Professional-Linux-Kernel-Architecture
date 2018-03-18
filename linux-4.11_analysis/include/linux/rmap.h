#ifndef _LINUX_RMAP_H
#define _LINUX_RMAP_H
/*
 * Declarations for Reverse Mapping functions in mm/rmap.c
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/rwsem.h>
#include <linux/memcontrol.h>
#include <linux/highmem.h>

/*
 * The anon_vma heads a list of private "related" vmas, to scan if
 * an anonymous page pointing to this anon_vma needs to be unmapped:
 * the vmas on the list will be related by forking, or by splitting.
 *
 * Since vmas come and go as they are split and merged (particularly
 * in mprotect), the mapping field of an anonymous page cannot point
 * directly to a vma: instead it points to an anon_vma, on whose list
 * the related vmas can be easily linked or unlinked.
 *
 * After unlinking the last vma on the list, we must garbage collect
 * the anon_vma object itself: we're guaranteed no page can be
 * pointing to this anon_vma once its vma list is empty.
 */ 

//  P1
//                              P1 꺼
//  vma_P1 pvma          ----> anon_vma  <-------------------------
//  -------------------<-|--   --------------   |                 |
//  |                 |  | |   | root       |----                 |------    page
//  |  anon_vma       |--- |   | refcount-1 |                     |     |  --------------
//  |                 |    |   | parent     |                     |     |  |            |
//  |                 |    |   | degree-1   |                     |     |--| mapping    |
//  |  ...            |    |   | rb_root    |---------> +         |     |  -------------
//  |                 |    |   --------------   |     +   +       |     |    page 
//  |                 |    |                    |   +       +     |     |  --------------  
//  |                 |    |    vma_P1 꺼avc_P1 |  avc     avc_P1 |     |  |            |
//  |                 |    |    anon_vma_chain  |  ...            |     ---| mapping    |
//  |                 |    |   --------------   |  interval tree  |        -------------
//  |                 |    ----| vma        |   |                 |        ...
//  |                 |        | rb         |----                 |
//  |                 |        | anon_vma   |----------------------
//  |  anon_vma_chain |<------>| same_vma   |
//  -------------------        --------------

struct anon_vma {
	struct anon_vma *root;		/* Root of this anon_vma tree */
    // 현재 process 와 관련된 process 들의 anon_vma tree 에서 
    // 최초 anon_vma  즉 최초 process 의 anon_vma 
	struct rw_semaphore rwsem;	/* W: modification, R: walking the list */
	/*
	 * The refcount is taken on an anon_vma when there is no
	 * guarantee that the vma of page tables will exist for
	 * the duration of the operation. A caller that takes
	 * the reference is responsible for clearing up the
	 * anon_vma if they are the last user on release
	 */
	atomic_t refcount;
    // 현재 aon_vma 를 가리키는 child anon_vma 의 개수.
	/*
	 * Count of child anon_vmas and VMAs which points to this anon_vma.
	 * This counter is used for making decision about reusing anon_vma
	 * instead of forking new one. See comments in function anon_vma_clone.
	 */
	unsigned degree; 
    // anon_vma 와 연결된 child anon_vma
    //
    // P->C0->C1->C2, C1->C3, C1->C4 으로 프로세스가 생겼다 하면
    //
    //  parent process              P_anon_vma 
    //                                  |
    //                          --------------------------------
    //                          |               |               |
    //  child processes     C0_anon_vma     C1_anon_vma     C2_anon_vma 
    //                                          |
    //                                          |----------------
    //                                          |               |
    //                                      C3_anon_vma     C4_anon_vma
    // 
    //  이런 상황이면 C0~C4 의        root     는 P_anon_vma
    //                C3_anon_vma  의 parent   는 C1_anon_vma 
    //                P_anon_vma   의 refcount 는 5  
    //                C1_anon_vma  의 degree   는 2
    //                P_anon_vma   의 degree   는 3
    //
    //
	struct anon_vma *parent;	/* Parent of this anon_vma */ 
    // anon_vma tree 에서 현재 anon_vma 의 parent anon_vma 즉  
    // 현재 process 를 생성한 parent process 의 anon_vma 
	/*
	 * NOTE: the LSB of the rb_root.rb_node is set by
	 * mm_take_all_locks() _after_ taking the above lock. So the
	 * rb_root must only be read/written after taking the above lock
	 * to be sure to see a valid next pointer. The LSB bit itself
	 * is serialized by a system wide lock only visible to
	 * mm_take_all_locks() (mm_all_locks_mutex).
	 */
	struct rb_root rb_root;	/* Interval tree of private "related" vmas */ 
    // reverse mapping 을 위한 interval tree 로 여기에 
    // anon_vma_chain 들이 Interval tree 구조로 연결되어 있음
};

/*
 * The copy-on-write semantics of fork mean that an anon_vma
 * can become associated with multiple processes. Furthermore,
 * each child process will have its own anon_vma, where new
 * pages for that process are instantiated.
 *
 * This structure allows us to find the anon_vmas associated
 * with a VMA, or the VMAs associated with an anon_vma.
 * The "same_vma" list contains the anon_vma_chains linking
 * all the anon_vmas associated with this VMA.
 * The "rb" field indexes on an interval tree the anon_vma_chains
 * which link all the VMAs associated with this anon_vma.
 */
struct anon_vma_chain {
	struct vm_area_struct *vma; 
    // 이놈이 담당하고 있는 vma 로 interval tree 내의 
    // 각 node 연산에 사용될 start, end 를 가짐
	struct anon_vma *anon_vma;
    // 현재 avc interval tree 를 관리하는 
    // process 당 존재하는 anon_vma 
	struct list_head same_vma;   /* locked by mmap_sem & page_table_lock */ 
    // 이 avc 가 관리하는 vma  와 관련된 anon_vma 들...
    // 즉 fork 시, parent 의 vma 와 child 의 vma 가 연결됨
	struct rb_node rb;			/* locked by anon_vma->rwsem */ 
    // interval tree 내에서 속해있는 위치
	unsigned long rb_subtree_last; 
    // interval tree 에서 사용하는 값
#ifdef CONFIG_DEBUG_VM_RB
	unsigned long cached_vma_start, cached_vma_last;
#endif
};

// try to unmap flag 로 try_to_unmap, should_defer_flush, shrink_page_list 
// 의 함수 parameter 로 사용되어 page 를 reclaim 해야 될 때 사용된다. 
enum ttu_flags {
	TTU_UNMAP = 1,			/* unmap mode */
	TTU_MIGRATION = 2,		/* migration mode */
	TTU_MUNLOCK = 4,		/* munlock mode */
	TTU_LZFREE = 8,			/* lazy free mode */
	TTU_SPLIT_HUGE_PMD = 16,	/* split huge PMD if any */

	TTU_IGNORE_MLOCK = (1 << 8),	/* ignore mlock */
	TTU_IGNORE_ACCESS = (1 << 9),	/* don't age */
	TTU_IGNORE_HWPOISON = (1 << 10),/* corrupted page is recoverable */
	TTU_BATCH_FLUSH = (1 << 11),	/* Batch TLB flushes where possible
					 * and caller guarantees they will
					 * do a final flush if necessary */
	TTU_RMAP_LOCKED = (1 << 12)	/* do not grab rmap lock:
					 * caller holds it */
};

#ifdef CONFIG_MMU
static inline void get_anon_vma(struct anon_vma *anon_vma)
{
	atomic_inc(&anon_vma->refcount);
}

void __put_anon_vma(struct anon_vma *anon_vma);

static inline void put_anon_vma(struct anon_vma *anon_vma)
{
	if (atomic_dec_and_test(&anon_vma->refcount))
		__put_anon_vma(anon_vma);
}

static inline void anon_vma_lock_write(struct anon_vma *anon_vma)
{
	down_write(&anon_vma->root->rwsem);
}

static inline void anon_vma_unlock_write(struct anon_vma *anon_vma)
{
	up_write(&anon_vma->root->rwsem);
}

static inline void anon_vma_lock_read(struct anon_vma *anon_vma)
{
	down_read(&anon_vma->root->rwsem);
}

static inline void anon_vma_unlock_read(struct anon_vma *anon_vma)
{
	up_read(&anon_vma->root->rwsem);
}


/*
 * anon_vma helper functions.
 */
void anon_vma_init(void);	/* create anon_vma_cachep */
int  __anon_vma_prepare(struct vm_area_struct *);
void unlink_anon_vmas(struct vm_area_struct *);
int anon_vma_clone(struct vm_area_struct *, struct vm_area_struct *);
int anon_vma_fork(struct vm_area_struct *, struct vm_area_struct *);

static inline int anon_vma_prepare(struct vm_area_struct *vma)
{
	if (likely(vma->anon_vma))
		return 0;

	return __anon_vma_prepare(vma);
}

static inline void anon_vma_merge(struct vm_area_struct *vma,
				  struct vm_area_struct *next)
{
	VM_BUG_ON_VMA(vma->anon_vma != next->anon_vma, vma);
	unlink_anon_vmas(next);
}

struct anon_vma *page_get_anon_vma(struct page *page);

/* bitflags for do_page_add_anon_rmap() */
#define RMAP_EXCLUSIVE 0x01
#define RMAP_COMPOUND 0x02

/*
 * rmap interfaces called when adding or removing pte of page
 */
void page_move_anon_rmap(struct page *, struct vm_area_struct *);
void page_add_anon_rmap(struct page *, struct vm_area_struct *,
		unsigned long, bool);
void do_page_add_anon_rmap(struct page *, struct vm_area_struct *,
			   unsigned long, int);
void page_add_new_anon_rmap(struct page *, struct vm_area_struct *,
		unsigned long, bool);
void page_add_file_rmap(struct page *, bool);
void page_remove_rmap(struct page *, bool);

void hugepage_add_anon_rmap(struct page *, struct vm_area_struct *,
			    unsigned long);
void hugepage_add_new_anon_rmap(struct page *, struct vm_area_struct *,
				unsigned long);

static inline void page_dup_rmap(struct page *page, bool compound)
{
	atomic_inc(compound ? compound_mapcount_ptr(page) : &page->_mapcount);
}

/*
 * Called from mm/vmscan.c to handle paging out
 */
int page_referenced(struct page *, int is_locked,
			struct mem_cgroup *memcg, unsigned long *vm_flags);

#define TTU_ACTION(x) ((x) & TTU_ACTION_MASK)

int try_to_unmap(struct page *, enum ttu_flags flags);

/* Avoid racy checks */
#define PVMW_SYNC		(1 << 0)
/* Look for migarion entries rather than present PTEs */
#define PVMW_MIGRATION		(1 << 1)

struct page_vma_mapped_walk {
	struct page *page;
    // rmap 에서 확인할 page 
    // frame 에 해당하는 page 
	struct vm_area_struct *vma;
    // Interval tree 에서 찾은 
    // avc 가 가진 vma 로 이 vma 
    // 의 pte 찾아 접근
	unsigned long address; 
    // pte 찾을 virtual address
	pmd_t *pmd;
    // pmd 주소
	pte_t *pte;
    // pte 주소
	spinlock_t *ptl;
    // pte 접근시 사용할 lock
	unsigned int flags;
};

static inline void page_vma_mapped_walk_done(struct page_vma_mapped_walk *pvmw)
{
	if (pvmw->pte)
		pte_unmap(pvmw->pte);
	if (pvmw->ptl)
		spin_unlock(pvmw->ptl);
}

bool page_vma_mapped_walk(struct page_vma_mapped_walk *pvmw);

/*
 * Used by swapoff to help locate where page is expected in vma.
 */
unsigned long page_address_in_vma(struct page *, struct vm_area_struct *);

/*
 * Cleans the PTEs of shared mappings.
 * (and since clean PTEs should also be readonly, write protects them too)
 *
 * returns the number of cleaned PTEs.
 */
int page_mkclean(struct page *);

/*
 * called in munlock()/munmap() path to check for other vmas holding
 * the page mlocked.
 */
int try_to_munlock(struct page *);

void remove_migration_ptes(struct page *old, struct page *new, bool locked);

/*
 * Called by memory-failure.c to kill processes.
 */
struct anon_vma *page_lock_anon_vma_read(struct page *page);
void page_unlock_anon_vma_read(struct anon_vma *anon_vma);
int page_mapped_in_vma(struct page *page, struct vm_area_struct *vma);

/*
 * rmap_walk_control: To control rmap traversing for specific needs
 *
 * arg: passed to rmap_one() and invalid_vma()
 * rmap_one: executed on each vma where page is mapped
 * done: for checking traversing termination condition
 * anon_lock: for getting anon_lock by optimized way rather than default
 * invalid_vma: for skipping uninterested vma
 */ 
// 
// reverse mapping 관련 control structure
struct rmap_walk_control {
	void *arg;
    // rmap_one 함수에 전달될 argument
	int (*rmap_one)(struct page *page, struct vm_area_struct *vma,
					unsigned long addr, void *arg);
    // page 를 가리키는 pte 를 찾아 PAGE_ACCESSED bit 를 clear 하는 함수 
    // e.g. page_referenced_one
	int (*done)(struct page *page);
	struct anon_vma *(*anon_lock)(struct page *page);
    // page 와 관련된 anon_vma 를 lock 잡으며 가져오는 함수
	bool (*invalid_vma)(struct vm_area_struct *vma, void *arg);
    // VM_LOCKED 이거나 VM_MAYSHARE 등 page out 되면 안되는 page 
    // 들을 skip 하는 함수
};

int rmap_walk(struct page *page, struct rmap_walk_control *rwc);
int rmap_walk_locked(struct page *page, struct rmap_walk_control *rwc);

#else	/* !CONFIG_MMU */

#define anon_vma_init()		do {} while (0)
#define anon_vma_prepare(vma)	(0)
#define anon_vma_link(vma)	do {} while (0)

static inline int page_referenced(struct page *page, int is_locked,
				  struct mem_cgroup *memcg,
				  unsigned long *vm_flags)
{
	*vm_flags = 0;
	return 0;
}

#define try_to_unmap(page, refs) SWAP_FAIL

static inline int page_mkclean(struct page *page)
{
	return 0;
}


#endif	/* CONFIG_MMU */

/*
 * Return values of try_to_unmap
 */
#define SWAP_SUCCESS	0
#define SWAP_AGAIN	1
#define SWAP_FAIL	2
#define SWAP_MLOCK	3
#define SWAP_LZFREE	4

#endif	/* _LINUX_RMAP_H */
