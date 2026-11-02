/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include <round.h>

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
	
	file_page->file = page->file.file;


	return true;
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
}

bool
mmap_lazy_load_segment (struct page *page, void *aux_p) {
	/* file_page 에 저장 */
	struct file_info *aux = (struct file_info *) aux_p;
	page->file = (struct file_page) { aux->file, aux->ofs, aux->read_bytes, aux->zero_bytes, aux->page_cnt };

	return lazy_load_segment (page, aux);

}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t ofs) {
	size_t read_bytes = length > file_length(file) - ofs ? file_length(file) - ofs : length;
	size_t zero_bytes = (ROUND_UP (length, PGSIZE) - read_bytes);
	int page_cnt = length % PGSIZE > 0 ? length / PGSIZE + 1 : length / PGSIZE;
	void *start_addr = addr;

	while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

        struct file_info *aux = malloc (sizeof *aux);
        if (aux == NULL) return NULL;

		/* 파일을 닫거나 삭제해도 그 파일의 mapping 유지 */
		struct file *new_file = file_reopen (file);
		if (new_file == NULL) {
			free (aux);
			return NULL;
		}
        *aux = (struct file_info) { new_file, ofs, page_read_bytes, page_zero_bytes, true, page_cnt };

        if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable,
                    mmap_lazy_load_segment, aux)) {
            free (aux);
			file_close (new_file);
            return NULL;
        }

        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        addr += PGSIZE;
        ofs += PGSIZE;
    }

	return start_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
}
