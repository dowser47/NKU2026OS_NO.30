#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <../sync/sync.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
#ifdef PMM_USE_BEST_FIT
    pmm_manager = &best_fit_pmm_manager;
#else
    pmm_manager = &default_pmm_manager;
#endif
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

/* 与原始 lab3 框架保持一致：对空闲链表操作前保存 SIE 并关闭本 hart 中断，
 * 操作后按原状态恢复。不能无条件开中断，否则嵌套临界区或 Trap 内分配会出错。
 * 这适用于实验的单 hart；多 hart 还需要自旋锁等跨核互斥。 */
// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        page = pmm_manager->alloc_pages(n);
    }
    local_intr_restore(intr_flag);
    return page;
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

/* lab2: 2414067 —— 从 DTB 得到内存范围，建立原框架的 Page 描述符数组。
 * npage 表示最高物理页号的上界，不是可分配页数；数组下标要减 nbase。
 * 固件、内核和 Page 数组对应的页保留，只将其后的完整页交给分配器。
 */
static void page_init(void) {
    uintptr_t base = get_memory_base();
    uintptr_t size = get_memory_size();
    assert(base == DRAM_BASE && size > 0 && size <= (uintptr_t)-1 - base);
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
    uintptr_t detected_end = base + size;
    /* 启动框架只承诺 KERNBASE～KERNTOP 的内存窗口，避免超出该上界。 */
    uintptr_t limit = KERNTOP - va_pa_offset;
    uintptr_t maxpa = ROUNDDOWN(detected_end < limit ? detected_end : limit, PGSIZE);
    assert(maxpa > base);
    npage = maxpa / PGSIZE;

    extern char end[];
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
    uintptr_t desc_pa = PADDR(pages);
    size_t count = npage - nbase;
    assert(desc_pa >= KERNBASE - va_pa_offset && desc_pa < maxpa);
    assert(count <= (maxpa - desc_pa) / sizeof(struct Page));
    memset(pages, 0, count * sizeof(struct Page));
    for (size_t i = 0; i < count; ++i) {
        SetPageReserved(pages + i);
        list_init(&pages[i].page_link);
    }
    uintptr_t freemem = ROUNDUP(desc_pa + count * sizeof(struct Page), PGSIZE);
    assert(freemem < maxpa);
    cprintf("physical memory: [0x%016lx, 0x%016lx)\n", base, maxpa);
    cprintf("Page descriptors: [0x%016lx, 0x%016lx), sizeof(Page)=%lu\n",
            desc_pa, desc_pa + count * sizeof(struct Page), sizeof(struct Page));
    cprintf("free memory: [0x%016lx, 0x%016lx), pages=%lu\n",
            freemem, maxpa, (maxpa - freemem) / PGSIZE);
    init_memmap(pa2page(freemem), (maxpa - freemem) / PGSIZE);
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
