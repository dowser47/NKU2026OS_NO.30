#ifndef __KERN_SYNC_SYNC_H__
#define __KERN_SYNC_SYNC_H__

#include <defs.h>
#include <intr.h>
#include <riscv.h>

/* 记录进入临界区前的 SIE：原本开启才关闭并返回真。
 * 若 Trap 入口或外层临界区已经关闭中断，则返回假；退出时不能擅自开启。
 * 嵌套示例：外层保存 1，内层保存 0；先退出内层仍关闭，最后退出外层才开启。 */
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}

/* flag 是“进入时是否开启”，不是“退出时是否想开启”。 */
static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

/* do { ... } while (0) 是 C 语言中封装“语句宏”的常用写法：
 * 1. do 先执行一次宏体，再判断条件；条件恒为 0，所以宏体恰好执行一次，
 *    不是等待条件变化的循环，也不会反复调用 __intr_save()。
 * 2. 把整个宏包装成一条语句，调用处像函数调用一样在末尾写分号：
 *        local_intr_save(intr_flag);
 *    预处理展开后是：do { intr_flag = __intr_save(); } while (0);
 *    调用处的分号正好结束 do-while 语句，因此宏定义末尾不再写分号。
 * 3. 可安全用于不带大括号的 if/else：
 *        if (need_save)
 *            local_intr_save(intr_flag);
 *        else
 *            other_action();
 *    若宏只用 { ... } 包装，调用处的分号会变成块后的独立空语句，
 *    将 if 与 else 隔开而导致语法错误；若直接展开为多条语句，则只有
 *    第一条受 if 控制。do-while 包装可避免这两类问题，并提供块作用域。
 * 4. 当前宏体只有一条赋值语句，本身并非必须这样包装；沿用这一惯用写法，
 *    可使今后增加语句时仍保持上述调用形式。保存/关闭中断由 __intr_save()
 *    完成，do-while 包装本身不提供原子性或中断保护。参数 x 应是可赋值的
 *    变量，用于把进入前的中断状态带回调用者，退出临界区时据此恢复。
 */
#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
#define local_intr_restore(x) __intr_restore(x);

#endif /* !__KERN_SYNC_SYNC_H__ */
