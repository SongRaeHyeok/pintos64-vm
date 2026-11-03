/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

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

	struct file_page *file_page = &page->file;
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
	if (pml4_is_dirty (thread_current ()->pml4, page->va)){
		/* 디스크에 write back */
		file_write_at (file_page->file, page->frame->kva, file_page->read_bytes, file_page->ofs); 
	}

	pml4_clear_page (thread_current ()->pml4, page->va);
	hash_delete (&thread_current ()->spt.hash, &page->elem); 

	file_close (page->file.file);

	if(!page->frame) {
		palloc_free_page (page->frame->kva);
		page->frame->kva = NULL;
		free (page->frame);
	}
}

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
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

	return success;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

	length = file_length (file) - offset < length ? file_length (file) - offset : length; 
	int count = (length + PGSIZE - 1) / PGSIZE;

	while (length) {
		size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct file_info *aux = malloc (sizeof *aux);
		if (aux == NULL)
            return false;

 		*aux = (struct file_info) { file_reopen (file), offset, page_read_bytes, page_zero_bytes, count};
		
        if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable,
                    lazy_load_segment, aux)) {
            free (aux); /*TODO : 누수 처리*/
            return false;
        }

        addr += PGSIZE;
        offset += PGSIZE;
		length -= page_read_bytes;
		count = 0;
    }
}

/* Do the munmap */
void
do_munmap (void *addr) {
	/* 가상 페이지 만들고 spt에 page 추가, 물리 메모리 할당(lazy로 fault 났는 경우), 페이지 매핑 (페이지 테이블 작성), file 읽어옴 */
	struct page *page = spt_find_page (&thread_current ()->spt, addr);
	int count = page->file.page_cnt;

	while (count) {
		/*edge 케이스 처리*/
		vm_dealloc_page (page);
		addr += PGSIZE;
		count--;
	}

}
