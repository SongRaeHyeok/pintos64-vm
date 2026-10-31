/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "vm/uninit.h"
#include <string.h>
#include "userprog/process.h"

extern struct lock file_lock;
/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE (type) != VM_UNINIT);

	struct supplemental_page_table *spt = &thread_current ()->spt;

	if (spt_find_page (spt, upage) == NULL) {
		bool (*initializer) (struct page *, enum vm_type, void *);

		switch (VM_TYPE (type)) {
			case VM_ANON:
				initializer = anon_initializer;
				break;
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
			default:
				goto err;
		}

		struct page *page = malloc (sizeof *page);
		if (page == NULL)
			goto err;

		uninit_new (page, upage, init, type, aux, initializer);
		page->writable = writable;	
		
		if (!spt_insert_page (spt, page)) {
			free (page);
			goto err;
		}
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	struct page page;
	struct hash_elem *e;

	page.va = pg_round_down (va);
	e = hash_find (&spt->hash, &page.elem);

	if (e == NULL)
		return NULL;
	
	return hash_entry (e, struct page, elem);
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt, struct page *page) {
	bool succ = false;
	if (hash_insert (&spt->hash, &page->elem) == NULL)
		succ = true;

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
  struct frame *frame = malloc (sizeof *frame);
  if (frame == NULL)
    PANIC("malloc failed");

  frame->page = NULL;
  frame->kva = palloc_get_page (PAL_USER);

  if (frame->kva == NULL) {
    PANIC ("TODO");
  }
  /* 이후 frame table 생기면 거기서 처리할 일 고려해봐야 할듯 */

  ASSERT (frame != NULL);
  ASSERT (frame->page == NULL);
  return frame;
}

#define STACK_LIMIT (USER_STACK - (1 << 20))   /* 스택 최대 1MB */

/* addr이 스택을 키우려는 접근으로 보이는가 */
static bool
is_stack_access (void *addr, uintptr_t rsp) {
	uintptr_t a = (uintptr_t) addr;
	return STACK_LIMIT <= a && a < USER_STACK   /* 스택 구역 안 */
	       && a >= rsp - 8;                     /* rsp 근처 (PUSH는 rsp-8) */
}

/* addr이 속한 페이지를 스택 페이지로 만들어 바로 연결 */
static bool
vm_stack_growth (void *addr) {
	void *va = pg_round_down (addr);
	return vm_alloc_page (VM_ANON | VM_MARKER_0, va, true) && vm_claim_page (va);
}

/* 스택 접근이면 키운다 (page fault와 시스템 콜 검사가 같이 사용) */
bool
vm_try_stack_growth (void *addr, uintptr_t rsp) {
	return is_stack_access (addr, rsp) && vm_stack_growth (addr);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user, bool write, bool not_present) {
	if (addr == NULL || is_kernel_vaddr (addr) || !not_present)
		return false;

	struct thread *curr = thread_current ();
	struct page *page = spt_find_page (&curr->spt, addr);

	if (page == NULL)   /* SPT에 없음 → 스택 증가 후보 */
		return vm_try_stack_growth (addr, user ? f->rsp : curr->user_rsp);
		
	if (write && !page->writable)
		return false;

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
	struct page *page = spt_find_page (&thread_current ()->spt, va);
	if (page == NULL)
		return false;

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {

	ASSERT (page->frame == NULL);
	
	struct frame *frame = vm_get_frame ();
	struct thread *curr = thread_current ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	if (!pml4_set_page (curr->pml4, page->va, frame->kva, page->writable) || !swap_in (page, frame->kva)) {
		frame->page = NULL;
		page->frame = NULL;
		if (pml4_get_page (curr->pml4, page->va))
			pml4_clear_page (curr->pml4, page->va);

		palloc_free_page (frame->kva);
		free (frame);
		return false;
	}
	return true;
}

/* Returns a hash value for page p. */
uint64_t
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, elem);
  return hash_bytes (&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, elem);
  const struct page *b = hash_entry (b_, struct page, elem);

  return a->va < b->va;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	hash_init (&spt->hash, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
			
	ASSERT (dst == &thread_current ()->spt);

	/* hash 설정 */
	struct hash_iterator i;
	hash_first(&i, &src->hash);

	/* spt 테이블 순회 */
	while (hash_next(&i)) {
		struct page *page = hash_entry (hash_cur (&i), struct page, elem);
		
		struct file_info *aux = NULL;
		struct file *file = NULL;

		/* check vm_type */
		enum vm_type vm_type = VM_TYPE(page->operations->type);
		vm_initializer *initializer = vm_type == VM_UNINIT ? page->uninit.init : NULL;

		/* uninit 일 때 처리 */
		if (vm_type == VM_UNINIT) {
			aux = malloc (sizeof *aux);
			if (aux == NULL) return false;

			/* file_info 복사 */
			struct file_info *old_aux = page->uninit.aux;

			bool held = lock_held_by_current_thread (&file_lock);
			if (!held) lock_acquire (&file_lock);
			file = file_duplicate (old_aux->file);
			if (!held) lock_release (&file_lock);

			*aux = (struct file_info) { file, old_aux->ofs, old_aux->read_bytes, old_aux->zero_bytes, .is_aux_copied = true };
	
		} 

		/* 페이지 생성 */
		if (!vm_alloc_page_with_initializer (page_get_type(page), page->va, 
			page->writable, initializer, aux)) {
				if (aux != NULL && aux->is_aux_copied)
					file_close (file);
				free (aux);
				return false;
		}
		
		/* anon 일 때 처리 */
		if (vm_type == VM_ANON) {
			if (!vm_claim_page (page->va)) {
				return false;
			}
			memcpy (spt_find_page (dst, page->va)->frame->kva, page->frame->kva, PGSIZE);
		}

		/* file 일 때 처리 */
		if (vm_type == VM_FILE) {
			PANIC ("TODO");
		}
	}
	
	return true;
}

void
hash_page_clear (struct hash_elem *e, void *aux) {
	struct page *page = hash_entry (e, struct page, elem);
	vm_dealloc_page (page);	
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	hash_clear (&spt->hash, hash_page_clear);
}
