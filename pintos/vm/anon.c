/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/malloc.h"
#include "lib/kernel/bitmap.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	if (!(swap_disk = disk_get (1, 1)))
		PANIC ("Set up swap disk failed");
	if (!(swap_table->bitmap = bitmap_create (disk_size (swap_disk) / 8)))
		PANIC ("Bitmap create failed");
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	size_t sector_idx = anon_page->swap_slot * 8;

	for (int i = 0; i < 8; i++) {
		disk_read (swap_disk, sector_idx + i, page->frame->kva + i * DISK_SECTOR_SIZE);
	}
	bitmap_set (swap_table->bitmap, anon_page->swap_slot, false);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if (anon_page->swap_slot == BITMAP_ERROR)
		return false;
		
	size_t sector_idx = anon_page->swap_slot * 8;

	for (int i = 0; i < 8; i++) {
		disk_write (swap_disk, sector_idx + i, page->frame->kva + i * DISK_SECTOR_SIZE);
	}

	pml4_clear_page (page->pml4, page->va);
	
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	if (page->frame != NULL) {
		// pml4_clear_page (thread_current ()->pml4, page->va);
		list_remove (&page->frame->elem);
		free (page->frame);
	}
	else 
		bitmap_set (swap_table->bitmap, anon_page->swap_slot, false);
		 
	return;
}
