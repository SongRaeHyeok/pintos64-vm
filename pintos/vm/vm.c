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
static struct page *do_page_copy (struct page *src);

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
	struct frame *frame = malloc (sizeof (struct frame));
	if (frame == NULL)
		PANIC ("malloc failed");

	/* TODO: Fill this function. */
	frame->kva = palloc_get_page (PAL_USER);
	if (frame->kva == NULL)
		PANIC ("todo");
	frame->page = NULL;	

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr,
		bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if (addr == NULL || is_kernel_vaddr (addr))
		return false;
	
	/* spt 에서 조회 : fake fault 구분 */
	struct page *page = spt_find_page (spt, addr);
	
	if (!page || !not_present || (!page->writable && write)) {
		return false;
	} 	

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
bool
supplemental_page_table_copy (struct supplemental_page_table *dst, struct supplemental_page_table *src) {
	struct hash_iterator iter;

	hash_first (&iter, &src->hash);
	while (hash_next (&iter)) {
		struct page *page = hash_entry (iter.elem, struct page, elem);

		if (page->operations->type == VM_UNINIT) {
			struct page *tmp = do_page_copy (page);
			if (tmp == NULL)
				return false;

			if (!spt_insert_page (dst, tmp)) {
				do_free_page (tmp, tmp->uninit.aux);
				free (tmp);
				return false;
			}
			continue;
		}

		if (!vm_alloc_page (page->operations->type, page->va, page->writable) || !vm_claim_page (page->va)) {
			supplemental_page_table_kill (dst);
			return false;
		}

		if (page->frame != NULL) {
			memcpy (spt_find_page (dst, page->va)->frame->kva, page->frame->kva, PGSIZE);
		}
		else {
			/* TODO : 
			 * 부모 스왑 슬롯에서 읽어서 자식 프레임에 바로 올리기 - HJM
			 */
		}
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	struct hash_iterator iter;

	while (!hash_empty (&spt->hash)) {
		hash_first (&iter, &spt->hash);
		hash_next (&iter);
		struct page *page = hash_entry (iter.elem, struct page, elem);

		if (page->operations->type == VM_FILE) {
			PANIC ("Need to writeback");
		}

		destroy (page);
		hash_delete (&spt->hash, iter.elem);
		free (page);
	}
	free (spt->hash.buckets);
}

static struct page *
do_page_copy (struct page *src) {
	struct page *dst = malloc (sizeof (struct page));
	if (dst == NULL)
		return NULL;

	memcpy (dst, src, sizeof (struct page));

	if (src->uninit.aux != NULL) {
		struct file_info *file_info = malloc (sizeof (struct file_info));
		if (file_info == NULL) {
			free (dst);
			return NULL;
		}
		
		memcpy (file_info, src->uninit.aux, sizeof (struct file_info));
		dst->uninit.aux = file_info;

		struct file_info *src_file_info = (struct file_info *)src->uninit.aux;

		bool held = lock_held_by_current_thread (&file_lock);
		if (!held) lock_acquire (&file_lock);
		struct file *file = file_duplicate (src_file_info->file);
		if (!held) lock_release (&file_lock);

		if (file == NULL) {
			free (file_info);
			free (dst);
			return NULL;
		}

		file_info->file = file;
		file_info->own_file = true;
	}

	return dst;
}

void
do_free_page (struct page *page, void *aux) {

	struct file_info *file_info = (struct file_info *)aux;
	if (file_info->own_file) {
		bool held = lock_held_by_current_thread (&file_lock);
		if (!held) lock_acquire (&file_lock);
		file_close (file_info->file);
		if (!held) lock_release (&file_lock);
	}
	free (file_info);
}