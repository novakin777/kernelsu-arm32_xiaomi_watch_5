[English version](README.md)

# KernelSU ARM32 для Xiaomi Watch 5


Форк [KernelSU](https://github.com/Justadeayo/KernelSU) с изменениями для запуска 32-битного userspace на Xiaomi Watch 5 (`blenny`). Это экспериментальная адаптация, а не универсальная сборка KernelSU.

## Что изменено

- Добавлена обработка ARM32 compat syscall table на 64-битном ARM64-ядре: если заголовки ядра не задают `__NR_compat_syscalls`, используется граница `452` (последний номер syscall — `451`).
- Workflow собирает `ksud` как ELF32 ARM (`armv7-linux-androideabi`) и помещает его в Manager под ABI `armeabi-v7a`.
- Для тестовой сборки в APK встраивается отдельный ELF64 AArch64 модуль KernelSU для KMI `android15-6.6`.

ABI приложения и userspace не определяет архитектуру модуля ядра. В частности, 32-битный Manager/`ksud` здесь работает с 64-битным ARM64 модулем. Встроенный модуль предназначен только для соответствующего KMI; он не является образом ядра и не заменяет модуль, собранный под другое устройство или ядро.

## Сборка и получение APK

В GitHub Actions вручную запустите workflow **Build ARM32 KernelSU Manager** (`workflow_dispatch`). Он собирает модуль для `android15-6.6`, ARM32 `ksud` и тестовый APK. После успешного запуска скачайте artifact `kernelsu-arm32-sigsys-fix` со страницы Actions.

Готовую сборку также можно скачать на странице [Releases](https://github.com/novakin777/kernelsu-arm32_xiaomi_watch_5/releases).

APK использует тестовый package ID `me.weishu.kernelsu.arm32test` и подписывается для тестовых сборок. Установка APK с другой подписью поверх уже установленной сборки может потребовать удалить прежнее приложение.

## Совместимость и ограничения

- Целевая конфигурация Manager: ARM32 `armeabi-v7a`.
- Встроенный модуль: ARM64, KMI `android15-6.6`.
- Изменение syscall table само по себе не означает поддержку любого ARM32 приложения или любого ядра.
- Установка и работа дополнительных модулей KernelSU через Manager не тестировались.
- Для других устройств и KMI требуется отдельная адаптация владельцами этих устройств и проверка на соответствующем ядре.
- Сборка или установка APK не доказывает, что root работает на конкретном устройстве.
- Сочетание DFRoot и этой ARM32-сборки KernelSU успешно проверено на глобальной версии Xiaomi Watch 5 (Android 16, OS3.0.3.70.WO61WMIXM, загрузчик заблокирован); root независимо подтверждён результатом `uid=0(root)`.

## Связанные проекты

- [KernelSU upstream](https://github.com/tiann/KernelSU)
- [Исходная ветка, от которой сделан форк](https://github.com/Justadeayo/KernelSU)
- [DFRoot-сборка для Xiaomi Watch 5](https://github.com/novakin777/dfroot-xiaomi-watch-5)
- [WearBridgeADB](https://github.com/novakin777/WearBridgeADB) — может выполнять команды на часах с root-правами после того, как KernelSU предоставит их приложению WearBridge на часах. Само приложение root не получает.

## Авторы и лицензия

- [novakin777](https://github.com/novakin777) — изменения ARM32 compat и сопровождение этой ветки.
- [Justadeayo/KernelSU](https://github.com/Justadeayo/KernelSU) и [KernelSU](https://github.com/tiann/KernelSU) — исходные проекты; авторство исходного кода сохраняется за его авторами.

KernelSU распространяется на условиях лицензий, указанных в исходных файлах: код `kernel/` — GPL-2.0-only, остальные компоненты — GPL-3.0-or-later. См. [LICENSE](LICENSE) и заголовки файлов.

## Использование ИИ

При подготовке изменений кода и документации использовались инструменты ИИ.
