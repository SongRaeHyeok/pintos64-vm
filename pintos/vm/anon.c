/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/malloc.h"
#include <bitmap.h>
#include "threads/vaddr.h"

#define SECTORS_PER_PAGE (PGSIZE / DISK_SECTOR_SIZE)

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
	/* 스왑 디스크 설정 */
	swap_disk = disk_get (1, 1);

	/* 스왑 디스크와 자료구조 연결 */
	swap_table = malloc (sizeof (struct swap_table));
	disk_sector_t cnt = disk_size (swap_disk);
	swap_table->bitmap = bitmap_create ( cnt / SECTORS_PER_PAGE );
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;

	/* swap slot 초기화 */
	anon_page->swap_slot = SIZE_MAX;

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	disk_sector_t st = anon_page->swap_slot * SECTORS_PER_PAGE;

	/* swap disk 에서 데이터를 읽어 메모리로 가져옴 */
	for (int i = 0; i < SECTORS_PER_PAGE; i++) {
		disk_read (swap_disk, st, kva);
		kva += DISK_SECTOR_SIZE;
		st ++;
	}
	
	/* swap table 갱신 */
	bitmap_set (swap_table->bitmap, anon_page->swap_slot, false);
	anon_page->swap_slot = SIZE_MAX;

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	/* 빈 swop slot 찾기 */
	size_t idx = bitmap_scan_and_flip (swap_table->bitmap, 0, 1, false);
	/* 디스크에 빈 slot 없으면 커널 panic */
	if (idx == BITMAP_ERROR) PANIC ("swap disk full");

	void *page_kva = page->frame->kva;
	disk_sector_t st = idx * SECTORS_PER_PAGE;

	/* slot 에 페이지 데이터 한 페이지 분량 복사 */
	for (int i = 0; i < SECTORS_PER_PAGE; i++) {
		disk_write (swap_disk, st, page_kva);
		page_kva += DISK_SECTOR_SIZE;
		st ++;
	}

	/* 데이터가 어디 있는지 page 구조체에 저장 */
	anon_page->swap_slot = idx;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	/* swap slot 칸 해제 */
	if (anon_page->swap_slot != SIZE_MAX)
		bitmap_set (swap_table->bitmap, anon_page->swap_slot, false);

	if (page->frame != NULL) {
		list_remove (&page->frame->elem);
		free (page->frame);
	}
}
