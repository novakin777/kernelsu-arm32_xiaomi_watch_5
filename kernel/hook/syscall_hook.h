#ifndef __KSU_H_KSU_SYSCALL_HOOK
#define __KSU_H_KSU_SYSCALL_HOOK
#include <asm/syscall.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/compat.h>
#include <asm/unistd.h>

/* Some Android 6.12 vendor headers omit the compat syscall count even when
 * CONFIG_COMPAT is enabled. ARM32 syscall numbers currently run through 451. */
#if defined(__aarch64__) && defined(CONFIG_COMPAT) && !defined(__NR_compat_syscalls)
#define __NR_compat_syscalls 452
#endif

#if defined(__x86_64__)
typedef sys_call_ptr_t syscall_fn_t;
#elif defined(__riscv)
typedef long (*syscall_fn_t)(const struct pt_regs *);
#endif

extern syscall_fn_t *ksu_syscall_table;

#if defined(__aarch64__) && defined(CONFIG_COMPAT)
extern syscall_fn_t *ksu_compat_syscall_table;
#endif

static inline syscall_fn_t *ksu_syscall_table_for_current(void)
{
#if defined(__aarch64__) && defined(CONFIG_COMPAT)
    if (unlikely(is_compat_task()))
        return READ_ONCE(ksu_compat_syscall_table);
#endif
    return READ_ONCE(ksu_syscall_table);
}

static inline long ksu_call_syscall(int nr, const struct pt_regs *regs)
{
    syscall_fn_t *table = ksu_syscall_table_for_current();

    if (!table || nr < 0)
        return -ENOSYS;
#if defined(__aarch64__) && defined(CONFIG_COMPAT)
    if (is_compat_task()) {
        if (nr >= __NR_compat_syscalls)
            return -ENOSYS;
    } else
#endif
#if defined(__aarch64__)
    if (nr >= __NR_syscalls)
        return -ENOSYS;
#endif
    return table[nr](regs);
}

// Dispatcher slot number in syscall table
extern int ksu_dispatcher_nr;

// Syscall hook handler type.
// orig_nr: the original syscall number before redirection
// regs: the original pt_regs from userspace
// Handler is responsible for calling ksu_call_syscall(orig_nr, regs) if needed.
typedef long (*ksu_syscall_hook_fn)(int orig_nr, const struct pt_regs *regs);

#if defined(__aarch64__) && defined(CONFIG_COMPAT)
int ksu_register_compat_syscall_hook(int nr, ksu_syscall_hook_fn fn);
void ksu_unregister_compat_syscall_hook(int nr);
bool ksu_redirect_compat_syscall(struct pt_regs *regs, int nr);
#endif

// --- Dispatcher-based hook API (register/unregister) ---
// Register a handler into the dispatcher's routing table for syscall @nr.
// When a marked process invokes syscall @nr, the sys_enter tracepoint redirects
// it to the unified dispatcher, which looks up @fn by @nr and calls it.
// Does NOT modify the syscall table itself — the dispatcher slot is shared.
// Returns 0 on success, -EEXIST if already registered, -EINVAL if nr invalid.
int ksu_register_syscall_hook(int nr, ksu_syscall_hook_fn fn);

// Remove a handler from the dispatcher's routing table for syscall @nr.
// The syscall table is not touched — only the dispatcher stops routing @nr.
void ksu_unregister_syscall_hook(int nr);

// Check if a handler is registered in the dispatcher for syscall @nr.
bool ksu_has_syscall_hook(int nr);

// --- Direct syscall table patching API (hook/unhook) ---
// Directly overwrite syscall_table[@nr] with @fn using fixmap + stop_machine.
// Saves the original handler to *@old (if non-NULL) and records the entry
// for restoration at module exit. Use this for boot-time hooks that replace
// a real syscall entry (e.g. ksud hooking __NR_execve/__NR_read/__NR_fstat).
void ksu_syscall_table_hook(int nr, syscall_fn_t fn, syscall_fn_t *old);

// Restore syscall_table[@nr] to its original value recorded by
// ksu_syscall_table_hook(), and remove the entry from the tracking list.
// Use this to cleanly undo a direct hook when it is no longer needed
// (e.g. ksud unhooking __NR_read after init.rc injection is done).
void ksu_syscall_table_unhook(int nr);

void ksu_syscall_hook_init(void);
void ksu_syscall_hook_exit(void);

#endif
