/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "vm/file.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/mmu.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))

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
	
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;

	/* 파일에서 내용 읽어 kva 의 page 로 swap in */
	bool held = lock_held_by_current_thread (&file_lock);
	if (!held) lock_acquire (&file_lock);
	file_read_at (file_page->file, kva, file_page->read_bytes, file_page->ofs);
	if (!held) lock_release (&file_lock);

	memset ((char *)(kva) + file_page->read_bytes, 0, file_page->zero_bytes);

	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	/* page 내용을 파일에 다시 써서 swap out */
	if (pml4_is_dirty (page->pml4, page->va)) {
		bool held = lock_held_by_current_thread (&file_lock);
		if (!held) lock_acquire (&file_lock);
		file_write_at (file_page->file, page->frame->kva, file_page->read_bytes, file_page->ofs);
		if (!held) lock_release (&file_lock);
		pml4_set_dirty (page->pml4, page->va, 0);
	}

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	// 프레임과 매핑, 변경된 파일 내용 등을 어떻게 정리할지 고민해라.
	struct file_page *file_page = &page->file;

	if (pml4_is_dirty (thread_current ()->pml4, page->va)) {
		bool held = lock_held_by_current_thread (&file_lock);
		if (!held) lock_acquire (&file_lock);
		file_write_at (file_page->file, page->frame->kva, file_page->read_bytes, file_page->ofs);
		if (!held) lock_release (&file_lock);
		pml4_set_dirty (thread_current ()->pml4, page->va, 0);
	}

	pml4_clear_page (thread_current ()->pml4, page->va);

	if (page->frame != NULL) {
		palloc_free_page (page->frame->kva);
		free (page->frame);
	}
	file_close (page->file.file);
}

static void
file_cleanup (struct supplemental_page_table *spt, void *addr, size_t mapped) {
	for (size_t i = 0; i < mapped; i++) {
		struct page *page = spt_find_page (spt, addr);
		spt_remove_page (spt, page);
		addr = (char *)addr + PGSIZE;
	}
}

static bool
file_initializer (struct page *page, void *aux) {
	struct file_info *fp = aux;

	struct file_page *file_page = &page->file;

		*file_page = (struct file_page) {
		.file = fp->file,
		.ofs = fp->ofs,
		.page_cnt = fp->page_cnt,
		.read_bytes = fp->read_bytes,
		.zero_bytes = fp->zero_bytes
	};
	free (fp);

	off_t read_bytes;

	bool held = lock_held_by_current_thread (&file_lock);
	if (!held) lock_acquire (&file_lock);
	read_bytes = file_read_at (file_page->file, page->frame->kva, file_page->read_bytes, file_page->ofs);
	if (!held) lock_release (&file_lock);

	if (read_bytes != file_page->read_bytes)
		return false;

	memset ((char *) page->frame->kva + file_page->read_bytes, 0, file_page->zero_bytes);

	return true;
}

static struct file_info*
fill_file_info (size_t length, size_t file_left, struct file *file, off_t ofs) {
	struct file_info *file_info = malloc (sizeof (*file_info));
	if (file_info == NULL)
		return NULL;

	uint32_t read_bytes = MIN (PGSIZE, MIN (length, file_left));
	uint32_t zero_bytes = PGSIZE - read_bytes;
	*file_info = (struct file_info) {
		.file = file_reopen (file),
		.ofs = ofs,
		.read_bytes = read_bytes,
		.zero_bytes = zero_bytes,
		.is_aux_copied = true
	};

	return file_info;
}

bool
do_mmap_helper (void *addr, size_t length, int writable, struct file *file, off_t offset) {
	
	off_t file_size = file_length (file);
	off_t current_ofs = offset;
	void *start = addr;
	size_t page_cnt = length / PGSIZE + (bool)(length % PGSIZE);
	size_t mapped = 0;
	for (size_t i = 0; i < page_cnt; i++) {
		size_t file_left = 0;

		if (current_ofs < file_size)
			file_left = file_size - current_ofs;

		struct file_info *file_info = fill_file_info (length, file_left, file, current_ofs);
		if (file_info == NULL) {
			file_cleanup (&thread_current ()->spt, start, mapped);
			return false;
		}

		if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable, file_initializer, file_info)) {
			file_cleanup (&thread_current ()->spt, start, mapped);
			free (file_info);
			return false;
		}

		length = length < PGSIZE ? 0 : length - PGSIZE;
		current_ofs += file_info->read_bytes;
		addr += PGSIZE;
		mapped++;

		file_info->page_cnt = page_cnt;
	};

	return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	
	if (do_mmap_helper (addr, length, writable, file, offset))
		return addr;
		
	return NULL;
}

static size_t
get_page_cnt (struct page *page) {

	size_t page_cnt = 0;
	enum vm_type type = page->operations->type;

	if (type == VM_FILE) 
		page_cnt = page->file.page_cnt;

	else if (type == VM_UNINIT && page->uninit.type == VM_FILE) {
		struct file_info *file_info = page->uninit.aux;
		page_cnt = file_info->page_cnt;
	}

	return page_cnt;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = spt_find_page (spt, addr);
	if (page == NULL)
		return;

	size_t page_cnt = get_page_cnt (page);

	for (size_t i = 0; i < page_cnt; i++) {
		file_cleanup (spt, addr, 1);
		addr = (char *)addr + PGSIZE;
	}
}