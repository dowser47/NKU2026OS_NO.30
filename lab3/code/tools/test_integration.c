/* 补充验证只由 verify_integration.py 复制到临时源码树，不进入常规内核。
 * 使用真实 CSR、原 PMM 实现和原 handler，避免仅凭输出判断临界区正确。
 */
#include <assert.h>
#include <pmm.h>
#include <intr.h>
#include <../sync/sync.h>
#include <trap.h>
#include <string.h>
#include <stdio.h>

extern void exception_handler(struct trapframe *tf);
static const struct pmm_manager *real_manager;
static unsigned alloc_calls, free_calls, count_calls;

static struct Page *guard_alloc(size_t n) {
    assert(!(read_csr(sstatus) & SSTATUS_SIE));
    ++alloc_calls;
    return real_manager->alloc_pages(n);
}
static void guard_free(struct Page *p, size_t n) {
    assert(!(read_csr(sstatus) & SSTATUS_SIE));
    ++free_calls;
    real_manager->free_pages(p, n);
}
static size_t guard_count(void) {
    assert(!(read_csr(sstatus) & SSTATUS_SIE));
    ++count_calls;
    return real_manager->nr_free_pages();
}

static void check_guards(void) {
    uintptr_t old_sie = read_csr(sie);
    bool was_enabled = (read_csr(sstatus) & SSTATUS_SIE) != 0;
    /* 尚未启动定时器；屏蔽来源，单独观察全局 SIE 的保存和恢复。 */
    write_csr(sie, 0);
    real_manager = pmm_manager;
    struct pmm_manager proxy = *real_manager;
    proxy.alloc_pages = guard_alloc;
    proxy.free_pages = guard_free;
    proxy.nr_free_pages = guard_count;
    pmm_manager = &proxy;
    for (int enabled = 0; enabled <= 1; ++enabled) {
        if (enabled) intr_enable(); else intr_disable();
        size_t before = nr_free_pages();
        assert(!!(read_csr(sstatus) & SSTATUS_SIE) == enabled);
        struct Page *p = alloc_pages(2);
        assert(p != NULL);
        assert(!!(read_csr(sstatus) & SSTATUS_SIE) == enabled);
        assert(nr_free_pages() == before - 2);
        assert(alloc_pages(before + 1) == NULL);
        assert(!!(read_csr(sstatus) & SSTATUS_SIE) == enabled);
        free_pages(p, 2);
        assert(!!(read_csr(sstatus) & SSTATUS_SIE) == enabled);
        assert(nr_free_pages() == before);
    }
    intr_enable();
    bool outer, inner;
    local_intr_save(outer);
    local_intr_save(inner);
    assert(outer && !inner);
    struct Page *p = alloc_page();
    assert(p != NULL && !(read_csr(sstatus) & SSTATUS_SIE));
    free_page(p);
    local_intr_restore(inner);
    assert(!(read_csr(sstatus) & SSTATUS_SIE));
    local_intr_restore(outer);
    assert(read_csr(sstatus) & SSTATUS_SIE);
    assert(alloc_calls == 5 && free_calls == 3 && count_calls == 6);
    intr_disable();
    pmm_manager = real_manager;
    write_csr(sie, old_sie);
    if (was_enabled) intr_enable();
    cprintf("[Integration] PMM interrupt guards: PASS\n");
}

static void check_exception_boundaries(void) {
    uint16_t code[4] __attribute__((aligned(4))) = {0};
    struct trapframe tf, expected;
    for (int kind = 0; kind < 2; ++kind) {
        for (unsigned low = 0; low < 4; ++low) {
            /* 只保证半字对齐，验证 handler 没有强制按 32 位地址取指。 */
            code[1] = low;
            memset(&tf, 0xa5, sizeof(tf));
            tf.cause = kind ? CAUSE_BREAKPOINT : CAUSE_ILLEGAL_INSTRUCTION;
            tf.epc = (uintptr_t)&code[1];
            expected = tf;
            expected.epc += low == 3 ? 4 : 2;
            exception_handler(&tf);
            assert(memcmp(&tf, &expected, sizeof(tf)) == 0);
        }
    }
    for (unsigned cause = 0; cause < 17; ++cause) {
        if (cause == CAUSE_BREAKPOINT || cause == CAUSE_ILLEGAL_INSTRUCTION) continue;
        memset(&tf, 0xa5, sizeof(tf));
        tf.cause = cause;
        tf.epc = 1;  /* 不可读地址：兜底分支应完全不取指，也不改变现场。 */
        expected = tf;
        exception_handler(&tf);
        assert(memcmp(&tf, &expected, sizeof(tf)) == 0);
    }
    cprintf("[Integration] exception boundaries: PASS (8 length + 15 fallback)\n");
}

void check_lab3_integration(void) {
    check_guards();
    check_exception_boundaries();
}
