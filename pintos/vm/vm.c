/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "vm/uninit.h"
#include <string.h>

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
        return NULL;

    frame->kva = palloc_get_page (PAL_USER | PAL_ZERO);
    if (frame->kva == NULL) {
        free (frame);
        return NULL;
    }

    frame->page = NULL;
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
	if (frame == NULL)
    	return false;

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

/* Initialize new supplemental page table. */
bool
supplemental_page_table_init (struct supplemental_page_table *spt) {
    spt->initialized = hash_init (&spt->hash, page_hash, page_less, NULL);
    return spt->initialized;
}

/* Copy supplemental page table from src to dst. */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
        struct supplemental_page_table *src) {
    /* vm_alloc_page와 vm_claim_page는 현재 스레드의 SPT를 사용한다. */
    ASSERT (dst == &thread_current ()->spt);

    if (!dst->initialized || !src->initialized)
        return false;

    struct hash_iterator i;
    hash_first (&i, &src->hash);

    while (hash_next (&i)) {
        struct page *parent_page = hash_entry (hash_cur (&i), struct page, elem);
        enum vm_type type = parent_page->operations->type;

        if (type == VM_UNINIT) {
            struct file_info *aux = NULL;

            /* 부모와 자식이 같은 aux를 해제하지 않도록 따로 복사한다. */
            if (parent_page->uninit.aux != NULL) {
                aux = malloc (sizeof *aux);
                if (aux == NULL)
                    return false;

                memcpy (aux, parent_page->uninit.aux, sizeof *aux);
            }

            if (!vm_alloc_page_with_initializer (
                    parent_page->uninit.type,
                    parent_page->va,
                    parent_page->writable,
                    parent_page->uninit.init,
                    aux)) {
                free (aux);
                return false;
            }
        } else {
            /* 현재 단계에서는 메모리에 있는 anonymous page만 처리. */
            if (type != VM_ANON || parent_page->frame == NULL)
                return false;

            if (!vm_alloc_page (type, parent_page->va, parent_page->writable))
                return false;
        }

        /* 생성한 uninit page에 frame을 연결하고 초기화한다. */
        if (!vm_claim_page (parent_page->va))
            return false;

        /* 이미 초기화된 부모 페이지는 현재 메모리 내용을 복사한다. */
        if (type != VM_UNINIT) {
            struct page *child_page = spt_find_page (dst, parent_page->va);

            memcpy (child_page->frame->kva, parent_page->frame->kva, PGSIZE);
        }
    }

    return true;
}

/* hash_destroy가 각 page에 대해 호출하는 함수. */
static void
page_free (struct hash_elem *e, void *aux UNUSED) {
    struct page *page = hash_entry (e, struct page, elem);
    vm_dealloc_page (page);
}

/* Free all resources owned by the supplemental page table. */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
    if (!spt->initialized)
        return;

    hash_destroy (&spt->hash, page_free);
    spt->initialized = false;
}