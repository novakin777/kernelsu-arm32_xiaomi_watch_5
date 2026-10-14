# KernelSU ARM32 for Xiaomi Watch 5

[Русский](README.md)

A [KernelSU](https://github.com/Justadeayo/KernelSU) fork with changes for a 32-bit userspace on the Xiaomi Watch 5 (`blenny`). This is an experimental adaptation, not a universal KernelSU build.

## Changes

- Adds ARM32 compat syscall table handling on a 64-bit ARM64 kernel: when kernel headers do not define `__NR_compat_syscalls`, the bound is set to `452` (the highest syscall number is `451`).
- The workflow builds `ksud` as ELF32 ARM (`armv7-linux-androideabi`) and packages it in the Manager under `armeabi-v7a`.
- The test APK embeds a separate ELF64 AArch64 KernelSU module for KMI `android15-6.6`.

The app/userspace ABI does not determine the kernel module architecture. Here, a 32-bit Manager/`ksud` works with a 64-bit ARM64 module. The embedded module is only for the matching KMI; it is not a kernel image and does not replace a module built for another device or kernel.

## Build and get the APK

In GitHub Actions, manually run **Build ARM32 KernelSU Manager** (`workflow_dispatch`). It builds the `android15-6.6` module, ARM32 `ksud`, and the test APK. After a successful run, download the `kernelsu-arm32-sigsys-fix` artifact from the Actions page.

Published builds are also available on the [Releases](https://github.com/novakin777/kernelsu-arm32_xiaomi_watch_5/releases) page.

The APK uses the test package ID `me.weishu.kernelsu.arm32test` and is signed for test builds. Installing an APK signed with a different key over an existing build may require uninstalling the previous app first.

## Compatibility and limits

- Manager target: ARM32 `armeabi-v7a`.
- Embedded module: ARM64, KMI `android15-6.6`.
- The syscall table change alone does not imply support for every ARM32 app or every kernel.
- Installing and using additional KernelSU modules through the Manager has not been tested.
- Other devices and KMIs require adaptation by their owners and validation on the corresponding kernel.
- Building or installing the APK does not prove that root works on a particular device.

## Related projects

- [KernelSU upstream](https://github.com/tiann/KernelSU)
- [KernelSU source fork](https://github.com/Justadeayo/KernelSU)
- [DFRoot build for Xiaomi Watch 5](https://github.com/novakin777/dfroot-xiaomi-watch-5)
- [WearBridgeADB](https://github.com/novakin777/WearBridgeADB) can run commands on the watch with root privileges after KernelSU grants root to the WearBridge app on the watch. WearBridge does not obtain root by itself.

## Authors and license

- [novakin777](https://github.com/novakin777) — ARM32 compat changes and maintenance of this branch.
- [Justadeayo/KernelSU](https://github.com/Justadeayo/KernelSU) and [KernelSU](https://github.com/tiann/KernelSU) — upstream projects; original authors retain attribution for their code.

KernelSU is distributed under the licenses stated in its source files: `kernel/` is GPL-2.0-only, while other components are GPL-3.0-or-later. See [LICENSE](LICENSE) and file headers.

## AI assistance

AI tools were used to assist with code changes and documentation.
