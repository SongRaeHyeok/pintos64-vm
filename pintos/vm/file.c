/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"

extern struct lock file_lock;

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	
	struct file_info *aux = page->uninit.aux;

	struct file_page *file_page = &page->file;
	*file_page = (struct file_page) {
		.file = aux->file,
		.ofs = aux->ofs,
		.read_bytes = aux->read_bytes,
		.zero_bytes = aux->zero_bytes,
		.page_cnt = aux->page_cnt,
	};
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	
	if (pml4_is_dirty (thread_current ()->pml4, page->va)) {
		struct file_page file_page = page->file;
		file_write_at (file_page.file, page->frame->kva, file_page.read_bytes, file_page.ofs);
	}
	pml4_clear_page (thread_current ()->pml4, page->va);
	hash_delete (&thread_current ()->spt.hash, &page->elem);
	file_close (page->file.file);
	if (page->frame != NULL) {
		palloc_free_page (page->frame->kva);
		page->frame->kva = NULL;
		free (page->frame);
		/* frame table 생성시 frame table 관리도 해줘야 함 */
	}
}

static bool
lazy_mmap_segment (struct page *page, void *aux) {
	bool success = true;
	struct file_info *file_info = (struct file_info *)aux;

	bool held = lock_held_by_current_thread (&file_lock);
	if (!held) lock_acquire (&file_lock);
	off_t read = file_read_at (file_info->file , page->frame->kva, file_info->read_bytes, file_info->ofs);
	if (!held) lock_release (&file_lock);

	if (read != file_info->read_bytes) {
		success = false;
	}
	memset ((char *)(page->frame->kva) + file_info->read_bytes, 0, file_info->zero_bytes);

	// free (aux->file);
	// free (aux);
	return success;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
	 struct file *file, off_t offset) {
	size_t size = length > file_length (file) - offset ? file_length (file) - offset : length;
	off_t page_ofs = 0;
	int cnt = (length + PGSIZE - 1) / PGSIZE;

	while (size > 0) {
		struct file_info *aux = malloc (sizeof *aux);
		if (aux == NULL)
			return NULL;
		
		uint32_t page_read_bytes = size >= PGSIZE ? PGSIZE : size % PGSIZE;
      
		*aux = (struct file_info) {
			.file = file_reopen (file),
			.ofs = offset,
			.read_bytes = page_read_bytes,
			.zero_bytes = PGSIZE - page_read_bytes,
			.page_cnt = cnt,
		};

		vm_alloc_page_with_initializer (VM_FILE, addr + page_ofs, writable, lazy_mmap_segment, aux);
		if (!spt_find_page (&thread_current ()->spt, addr + page_ofs)) {
			free (aux);
			return NULL;
		}

		size -= page_read_bytes;
		offset += PGSIZE;
		page_ofs += PGSIZE;
		cnt = 0;
	}
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct page *page = spt_find_page (&thread_current ()->spt, addr);
	if (page == NULL)
		return NULL;
		
	int cnt = page->file.page_cnt;
	while (cnt) {
		vm_dealloc_page (page);
		
		addr += PGSIZE;
		cnt--;
	}
}
