#ifdef __aarch64__

#include "../syscall_hook.h"

#include <linux/kallsyms.h>
#include <linux/mutex.h>
#include <asm/cacheflush.h>
#include <asm/unistd.h>
#include "infra/symbol_resolver.h"
#include "../patch_memory.h"
#include "arch.h"
#include "klog.h" // IWYU pragma: keep

syscall_fn_t *ksu_syscall_table = NULL;
int ksu_dispatcher_nr = -1;

#ifdef CONFIG_COMPAT
syscall_fn_t *ksu_compat_syscall_table;

#define KSU_COMPAT_HOOK_CAPACITY 7

struct compat_hook_entry {
    int nr;
    int dispatcher_nr;
    ksu_syscall_hook_fn fn;
};

static struct compat_hook_entry compat_hooks[KSU_COMPAT_HOOK_CAPACITY];
static int compat_hook_count;
static int compat_dispatch_slots[KSU_COMPAT_HOOK_CAPACITY];
static syscall_fn_t compat_dispatch_orig[KSU_COMPAT_HOOK_CAPACITY];
static int compat_dispatch_count;
#endif

// Hook registration table — read with READ_ONCE from tracepoint/dispatcher
// context, written with WRITE_ONCE from init/exit context.
static ksu_syscall_hook_fn syscall_hooks[__NR_syscalls];

// Track all hooked syscall entries for restoration.
// Protected by hooked_entries_lock.
struct syscall_hook_entry {
    int nr;
    syscall_fn_t orig;
};

static DEFINE_MUTEX(hooked_entries_lock);
static struct syscall_hook_entry hooked_entries[16];
static int hooked_count = 0;

#ifdef CONFIG_COMPAT
static int patch_compat_syscall(int nr, syscall_fn_t fn)
{
    if (!ksu_compat_syscall_table)
        return -ENOENT;
    if (nr < 0 || nr >= __NR_compat_syscalls)
        return -EINVAL;

    if (ksu_patch_text(&ksu_compat_syscall_table[nr], &fn, sizeof(fn), KSU_PATCH_TEXT_FLUSH_DCACHE)) {
        pr_err("patch compat syscall %d failed\n", nr);
        return -EIO;
    }
    return 0;
}

static long __nocfi ksu_compat_syscall_dispatcher(const struct pt_regs *regs)
{
    int i, count = READ_ONCE(compat_hook_count);
    int dispatcher_nr = regs->syscallno;

    /* A real AArch32 syscall keeps its original number in r7. */
    if (regs->regs[7] == dispatcher_nr)
        return -ENOSYS;

    for (i = 0; i < count; i++) {
        struct compat_hook_entry *entry = &compat_hooks[i];
        if (entry->dispatcher_nr == dispatcher_nr) {
            ((struct pt_regs *)regs)->syscallno = entry->nr;
            return entry->fn(entry->nr, regs);
        }
    }

    return -ENOSYS;
}

static int __init ksu_install_compat_dispatchers(void)
{
    unsigned long ni_syscall;
    int i, found = 0;

    ksu_compat_syscall_table = (syscall_fn_t *)
        ksu_resolve_symbol_for_functable_hook("compat_sys_call_table");
    if (!ksu_compat_syscall_table) {
        pr_warn("compat_sys_call_table unavailable; ARM32 syscall hooks disabled\n");
        return -ENOENT;
    }

    /* compat_sys_call_table uses the native NI wrapper for unused slots. */
    ni_syscall = (unsigned long)
        ksu_resolve_symbol_for_functable_hook("__arm64_sys_ni_syscall");
    if (!ni_syscall) {
        pr_warn("compat ni_syscall unavailable; ARM32 syscall hooks disabled\n");
        ksu_compat_syscall_table = NULL;
        return -ENOENT;
    }

    for (i = 0; i < __NR_compat_syscalls && found < KSU_COMPAT_HOOK_CAPACITY; i++) {
        if ((unsigned long)READ_ONCE(ksu_compat_syscall_table[i]) != ni_syscall)
            continue;

        compat_dispatch_slots[found] = i;
        compat_dispatch_orig[found] = READ_ONCE(ksu_compat_syscall_table[i]);
        if (patch_compat_syscall(i, (syscall_fn_t)ksu_compat_syscall_dispatcher))
            break;
        pr_info("compat dispatcher installed at slot %d\n", i);
        found++;
    }

    compat_dispatch_count = found;
    if (found != KSU_COMPAT_HOOK_CAPACITY) {
        while (found > 0) {
            found--;
            patch_compat_syscall(compat_dispatch_slots[found], compat_dispatch_orig[found]);
        }
        compat_dispatch_count = 0;
        ksu_compat_syscall_table = NULL;
        pr_warn("not enough compat ni_syscall slots; ARM32 syscall hooks disabled\n");
        return -ENOSPC;
    }

    return 0;
}

static void ksu_restore_compat_dispatchers(void)
{
    int i;

    if (!ksu_compat_syscall_table)
        return;

    for (i = 0; i < compat_dispatch_count; i++) {
        if (patch_compat_syscall(compat_dispatch_slots[i], compat_dispatch_orig[i]))
            pr_err("restore compat syscall %d failed\n", compat_dispatch_slots[i]);
    }
    compat_dispatch_count = 0;
    ksu_compat_syscall_table = NULL;
}

int ksu_register_compat_syscall_hook(int nr, ksu_syscall_hook_fn fn)
{
    int i, j, count = READ_ONCE(compat_hook_count), dispatcher_nr = -1;

    if (!ksu_compat_syscall_table || nr < 0 || nr >= __NR_compat_syscalls || !fn)
        return -EINVAL;

    for (i = 0; i < count; i++) {
        if (compat_hooks[i].nr == nr)
            return -EEXIST;
    }
    if (count >= compat_dispatch_count)
        return -ENOSPC;

    for (i = 0; i < compat_dispatch_count; i++) {
        bool in_use = false;
        for (j = 0; j < count; j++) {
            if (compat_hooks[j].dispatcher_nr == compat_dispatch_slots[i]) {
                in_use = true;
                break;
            }
        }
        if (!in_use) {
            dispatcher_nr = compat_dispatch_slots[i];
            break;
        }
    }
    if (dispatcher_nr < 0)
        return -ENOSPC;

    compat_hooks[count].nr = nr;
    compat_hooks[count].dispatcher_nr = dispatcher_nr;
    compat_hooks[count].fn = fn;
    WRITE_ONCE(compat_hook_count, count + 1);
    pr_info("registered compat syscall hook nr=%d via slot=%d\n", nr,
            compat_hooks[count].dispatcher_nr);
    return 0;
}

void ksu_unregister_compat_syscall_hook(int nr)
{
    int i, count = READ_ONCE(compat_hook_count);

    for (i = 0; i < count; i++) {
        if (compat_hooks[i].nr != nr)
            continue;
        while (i + 1 < count) {
            compat_hooks[i] = compat_hooks[i + 1];
            i++;
        }
        memset(&compat_hooks[count - 1], 0, sizeof(compat_hooks[0]));
        WRITE_ONCE(compat_hook_count, count - 1);
        return;
    }
}

bool ksu_redirect_compat_syscall(struct pt_regs *regs, int nr)
{
    int i, count = READ_ONCE(compat_hook_count);

    for (i = 0; i < count; i++) {
        if (compat_hooks[i].nr == nr) {
            regs->syscallno = compat_hooks[i].dispatcher_nr;
            return true;
        }
    }
    return false;
}
#endif

static int patch_syscall_table(int nr, syscall_fn_t fn)
{
    if (ksu_syscall_table == NULL)
        return -ENOENT;
    if (nr < 0 || nr >= __NR_syscalls)
        return -EINVAL;

    pr_info("patch syscall %d, 0x%lx -> 0x%lx\n", nr, (unsigned long)READ_ONCE(ksu_syscall_table[nr]),
            (unsigned long)fn);

    if (ksu_patch_text(&ksu_syscall_table[nr], &fn, sizeof(fn), KSU_PATCH_TEXT_FLUSH_DCACHE)) {
        pr_err("patch syscall %d failed\n", nr);
        return -EIO;
    }

    return 0;
}

// Direct syscall table patching: overwrite syscall_table[nr] with fn,
// save original to *old, and record for restoration at module exit.
void ksu_syscall_table_hook(int nr, syscall_fn_t fn, syscall_fn_t *old)
{
    if (ksu_syscall_table == NULL)
        return;
    if (nr < 0 || nr >= __NR_syscalls) {
        pr_info("invalid nr: %d\n", nr);
        return;
    }

    mutex_lock(&hooked_entries_lock);

    syscall_fn_t orig = READ_ONCE(ksu_syscall_table[nr]);
    if (old)
        *old = orig;

    // Record for later restoration
    int i;
    bool found = false;
    for (i = 0; i < hooked_count; i++) {
        if (hooked_entries[i].nr == nr) {
            found = true;
            break;
        }
    }
    if (!found) {
        if (hooked_count < ARRAY_SIZE(hooked_entries)) {
            hooked_entries[hooked_count].nr = nr;
            hooked_entries[hooked_count].orig = orig;
            hooked_count++;
        } else {
            pr_warn("hooked_entries full, cannot track syscall %d for restoration\n", nr);
        }
    }

    patch_syscall_table(nr, fn);

    mutex_unlock(&hooked_entries_lock);
}

// Restore syscall_table[nr] to its original value and remove from tracking list.
void ksu_syscall_table_unhook(int nr)
{
    int i;

    if (ksu_syscall_table == NULL)
        return;
    if (nr < 0 || nr >= __NR_syscalls)
        return;

    mutex_lock(&hooked_entries_lock);

    for (i = 0; i < hooked_count; i++) {
        if (hooked_entries[i].nr == nr) {
            patch_syscall_table(nr, hooked_entries[i].orig);
            // Remove entry by swapping with last
            hooked_entries[i] = hooked_entries[--hooked_count];
            mutex_unlock(&hooked_entries_lock);
            pr_info("unhooked syscall %d\n", nr);
            return;
        }
    }

    mutex_unlock(&hooked_entries_lock);
    pr_warn("syscall %d not found in hooked entries\n", nr);
}

static int __init ksu_find_ni_syscall_slots(int *out_slots, int max_slots)
{
    unsigned long ni_syscall;
    int i, count = 0;

    if (!ksu_syscall_table || max_slots <= 0)
        return 0;

    ni_syscall = (unsigned long)ksu_resolve_symbol_for_functable_hook("__arm64_sys_ni_syscall");

    pr_info("sys_ni_syscall: 0x%lx\n", ni_syscall);

    if (!ni_syscall)
        return 0;

    for (i = 0; i < __NR_syscalls && count < max_slots; i++) {
        if ((unsigned long)ksu_syscall_table[i] == ni_syscall) {
            out_slots[count++] = i;
            pr_info("ni_syscall %d: %d\n", count, i);
        }
    }

    return count;
}

// Unified dispatcher: reads original NR from x8, dispatches to handler.
// Validates that syscallno matches our dispatcher slot (i.e. we redirected it),
// otherwise it's a spurious call — return -ENOSYS.
static long __nocfi ksu_syscall_dispatcher(const struct pt_regs *regs)
{
    if (regs->syscallno != ksu_dispatcher_nr)
        return -ENOSYS;

    int orig_nr = (int)PT_REGS_ORIG_SYSCALL(regs);

    if (regs->syscallno == orig_nr)
        return -ENOSYS;

    // Restore registers to original state before dispatching
    ((struct pt_regs *)regs)->syscallno = orig_nr;
    PT_REGS_ORIG_SYSCALL((struct pt_regs *)regs) = orig_nr;

    if (likely(orig_nr >= 0 && orig_nr < __NR_syscalls)) {
        ksu_syscall_hook_fn fn = READ_ONCE(syscall_hooks[orig_nr]);
        if (likely(fn))
            return fn(orig_nr, regs);
    }

    return -ENOSYS;
}

// Register a handler into the dispatcher's routing table.
// Does not modify the syscall table — the dispatcher slot is shared by all hooks.
int ksu_register_syscall_hook(int nr, ksu_syscall_hook_fn fn)
{
    if (nr < 0 || nr >= __NR_syscalls)
        return -EINVAL;
    if (READ_ONCE(syscall_hooks[nr])) {
        pr_warn("syscall hook for nr=%d already registered, skip\n", nr);
        return -EEXIST;
    }
    WRITE_ONCE(syscall_hooks[nr], fn);
    pr_info("registered syscall hook for nr=%d\n", nr);
    return 0;
}

// Remove a handler from the dispatcher's routing table.
// The syscall table is not touched — only the dispatcher stops routing this nr.
void ksu_unregister_syscall_hook(int nr)
{
    if (nr < 0 || nr >= __NR_syscalls)
        return;
    WRITE_ONCE(syscall_hooks[nr], NULL);
    pr_info("unregistered syscall hook for nr=%d\n", nr);
}

bool ksu_has_syscall_hook(int nr)
{
    if (nr < 0 || nr >= __NR_syscalls)
        return false;
    return READ_ONCE(syscall_hooks[nr]) != NULL;
}

void __init ksu_syscall_hook_init(void)
{
    int ni_slot;

    memset(syscall_hooks, 0, sizeof(syscall_hooks));

    ksu_syscall_table = (syscall_fn_t *)ksu_resolve_symbol_for_functable_hook("sys_call_table");
    pr_info("sys_call_table=0x%lx", (unsigned long)ksu_syscall_table);

    if (!ksu_syscall_table)
        return;

#ifdef CONFIG_COMPAT
    ksu_install_compat_dispatchers();
#endif

    // Find one ni_syscall slot for the dispatcher
    if (ksu_find_ni_syscall_slots(&ni_slot, 1) < 1) {
        pr_err("failed to find ni_syscall slot for dispatcher\n");
        return;
    }

    ksu_dispatcher_nr = ni_slot;
    ksu_syscall_table_hook(ksu_dispatcher_nr, (syscall_fn_t)ksu_syscall_dispatcher, NULL);
    pr_info("dispatcher installed at slot %d\n", ksu_dispatcher_nr);
}

void __exit ksu_syscall_hook_exit(void)
{
    int i;

    if (!ksu_syscall_table)
        goto clear_state;

    // First, restore all patched syscall table entries while the dispatcher
    // and hook table are still intact, so in-flight syscalls see valid state.
    mutex_lock(&hooked_entries_lock);
    for (i = 0; i < hooked_count; i++) {
        int nr = hooked_entries[i].nr;
        syscall_fn_t orig = hooked_entries[i].orig;

        pr_info("restore syscall %d to 0x%lx\n", nr, (unsigned long)orig);
        if (ksu_patch_text(&ksu_syscall_table[nr], &orig, sizeof(orig), KSU_PATCH_TEXT_FLUSH_DCACHE)) {
            pr_err("restore syscall %d failed\n", nr);
        }
    }
    hooked_count = 0;
    mutex_unlock(&hooked_entries_lock);

#ifdef CONFIG_COMPAT
    ksu_restore_compat_dispatchers();
#endif

clear_state:
    // Now that the syscall table is restored, clear internal state.
    // At this point the tracepoint is already unregistered and synchronized
    // (done by ksu_syscall_hook_manager_exit before calling us), so no new
    // dispatches will occur.
    memset(syscall_hooks, 0, sizeof(syscall_hooks));
    ksu_dispatcher_nr = -1;

    pr_info("all syscall hooks restored\n");
}

#endif /* __aarch64__ */
