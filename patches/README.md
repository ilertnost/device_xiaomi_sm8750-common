# Патчи Evolution X для Xiaomi 15 (dada)

С 2026-11-22 upstream-репозитории не форкаются: в дереве лежит стоковый
AOSP / Evolution X / LineageOS, а все наши изменения хранятся в
`device/xiaomi/sm8750-common/patches` и применяются скриптом. Форками
остаются только device-specific репозитории (device tree, vendor blobs,
kernel) и `hardware/dolby` (в нём лежат проприетарные blobs HyperOS).

Схема взята из `android_device_xiaomi_dada`:
`device/xiaomi/dada/vendorsetup.sh` вызывает
`device/xiaomi/sm8750-common/patches/apply-patches.sh` при
`source build/envsetup.sh`.

## Раскладка

```
device/xiaomi/sm8750-common/patches/
    <путь-репозитория-через-подчёркивания>/
        NNNN-описание.patch
```

Имя каталога — это путь проекта от корня дерева, в котором `/` заменены на
`_`. Например:

```
frameworks_av/                              -> frameworks/av
frameworks_native/                          -> frameworks/native
external_crosvm/                            -> external/crosvm
external_rust_rutabaga_gfx/                 -> external/rust/rutabaga_gfx
packages_modules_Bluetooth/                 -> packages/modules/Bluetooth
packages_modules_Virtualization/            -> packages/modules/Virtualization
hardware_xiaomi/                            -> hardware/xiaomi
hardware_qcom-caf_sm8750_display_hal/       -> hardware/qcom-caf/sm8750/display/hal
vendor_qcom_opensource_display-core/        -> vendor/qcom/opensource/display-core
vendor_qcom_opensource_display-intf/        -> vendor/qcom/opensource/display-intf
vendor_qcom_opensource_interfaces/          -> vendor/qcom/opensource/interfaces
```

Если в пути проекта есть подчёркивание (`external/rust/rutabaga_gfx`), одной
замены `/` -> `_` недостаточно. В таком каталоге лежит файл `.project` с
настоящим путём:

```
external_rust_rutabaga_gfx/.project   -> external/rust/rutabaga_gfx
```

Внутри одного проекта патчи применяются в порядке имён файлов (`0001`,
`0002`, ...). Скрипт требует `git format-patch` (строку `From: ... <...>`);
plain diff он пропускает.

Каталоги, начинающиеся с `_`, скрипт не трогает:

- `_committed/` — старые патчи device/vendor-изменений, которые уже
  закоммичены в свои репозитории;
- `_optional/` — экспериментальные патчи (classic aptX Adaptive);
- `_forked-blob-repos/` — резервные копии патчей для репозиториев, которые
  остаются форками (например, `hardware/dolby`).

## Применение

Автоматически:

```bash
cd /data/AOSP/evo
source build/envsetup.sh
```

`vendorsetup.sh` запускает скрипт только если в
`.repo/local_manifests/dada.xml` есть заголовок patch-based-манифеста. Со
старым fork-манифестом патчи уже находятся в ветках форков, поэтому
автозапуск отключён, чтобы не применять их повторно.

Вручную:

```bash
cd /data/AOSP/evo
bash device/xiaomi/sm8750-common/patches/apply-patches.sh \
    device/xiaomi/sm8750-common/patches
```

Как работает скрипт:

- для каждого проекта берёт базовый коммит из `refs/remotes/m/` (ревизия
  манифеста);
- во временном индексе применяет все патчи проекта и сравнивает результат
  с рабочим деревом;
- если дерево уже совпадает — ничего не делает;
- если дерево чистое и стоит на базе — применяет патчи через `git am`,
  сохраняя автора и дату;
- если есть локальные изменения, незавершённый am/rebase/merge и т.п. —
  сообщает и оставляет дерево как есть.

## Манифест без форков

`device/xiaomi/sm8750-common/patches/dada-patches.xml` — версия
`.repo/local_manifests/dada.xml`, в которой upstream-проекты возвращены их
владельцам и закреплены на коммитах, для которых сгенерированы патчи:

| Проект | Владелец | База патчей |
|---|---|---|
| `frameworks/av` | Evolution-X | `80917b1522` |
| `frameworks/native` | Evolution-X | `8c23b549b2` |
| `external/crosvm` | AOSP | `c7dd8d6950` |
| `external/rust/rutabaga_gfx` | AOSP | `1f181b0b3e` |
| `packages/modules/Virtualization` | AOSP | `22f1c9ee92` |
| `hardware/xiaomi` | LineageOS | `fb0a2d51c7` |
| `hardware/qcom-caf/sm8750/display/hal` | LineageOS | `5b970807ee` |
| `vendor/qcom/opensource/display-core` | LineageOS | `89aa1914da` |
| `vendor/qcom/opensource/display-intf` | LineageOS | `19b5c055b4` |
| `packages/modules/Bluetooth` | Evolution-X | `00f2fc1131` |
| `vendor/qcom/opensource/interfaces` | LineageOS | `b75ec20e94` |

Переключение (после того как патчи проверены):

```bash
cd /data/AOSP/evo
mv .repo/local_manifests/dada.xml .repo/local_manifests/dada.xml.bak
cp device/xiaomi/sm8750-common/patches/dada-patches.xml \
   .repo/local_manifests/dada.xml
repo sync -j4
source build/envsetup.sh          # автоматически применит патчи
```

`dada-patches.xml` нельзя просто положить рядом с `dada.xml`: `repo` читает
все `*.xml` из `local_manifests/`, и проекты задвоятся.

Device/vendor-изменения уже закоммичены локально и не являются патчами:

- `device/xiaomi/sm8750-common` — `c9e0c3f` (BAP, `dual_bidirection_swb`,
  отключение subrating);
- `vendor/xiaomi/dada` — `d356cdb` (LC3 убран из A2DP-портов).

Эти коммиты пока не запушены. Их старые patch-файлы оставлены только как
резервные копии в `_committed/`.

`hardware/dolby` в новом манифесте остаётся форком `ilertnost`: это
репозиторий с HyperOS blobs, его патчи лежат в
`_forked-blob-repos/hardware/dolby/` и по умолчанию не применяются.

## Список патчей

| Проект | Патчей | Что делает |
|---|---:|---|
| `frameworks/av` | 1 | camera: implementation-defined stream sizes |
| `frameworks/native` | 1 | SF: не защищать client target для protected camera |
| `external/crosvm` | 2 | Android.bp visibility для vendor/platform |
| `external/rust/rutabaga_gfx` | 1 | Android.bp visibility |
| `packages/modules/Virtualization` | 1 | display client AIDL для platform |
| `hardware/xiaomi` | 10 | fingerprint, MiSys, IR, powershare, sensors, touchfeature, Euicc |
| `hardware/qcom-caf/sm8750/display/hal` | 3 | getDisplayAttributes, xiaomi_rgb_notifier, panel size |
| `vendor/qcom/opensource/display-core` | 3 | getDisplayAttributes, light sensor, panel size |
| `vendor/qcom/opensource/display-intf` | 1 | display attributes by hardware composer |
| `packages/modules/Bluetooth` | 2 | one CIS per channel; отключение subrating |
| `vendor/qcom/opensource/interfaces` | 1 | `codecSpecific` в classicVendorConfiguration |

Итого 26 активных патчей. Резервно: 2 патча `hardware/dolby`, 2 старых
device/vendor-патча в `_committed/`, 1 опциональный classic aptX.

## Опционально: classic aptX Adaptive (WIP)

`_optional/packages/modules/Bluetooth/0001-classic-aptx-adaptive-wip.patch`
— сохранённый net-diff ветки `backup/bt-local-20261121` (порт aptX Adaptive
из AOSPA + QTI `classicVendorConfiguration` + AVDT/AIDL фиксы). Скрипт его
не применяет: на classic A2DP aptX AD ранее ловили `Hardware Error 0x0f`,
а текущий стабильный стек — SBC/AAC. Применять вручную, только если
понадобится classic aptX AD:

```bash
git -C packages/modules/Bluetooth apply \
    device/xiaomi/sm8750-common/patches/_optional/packages/modules/Bluetooth/0001-classic-aptx-adaptive-wip.patch
```

Важно: **LE** aptX Adaptive (`0xff/0x000a/0x0001`) в стоковом EvoX-модуле
не поддержан. `system/bta/le_audio/le_audio_vendor_codec.h` знает только
Google vendor codec `0x00e0`; Qualcomm `0x000a` в backup-ветке тоже нет.
Чтобы earbuds переключались с LC3 на aptX Adaptive LE, нужен отдельный
порт Qualcomm vendor codec в `le_audio_vendor_codec.h`/`codec_manager.cc`
(в стоковой HyperOS эта поддержка есть).

## LE Audio: что выяснено по стоковой записи

Стоковая HyperOS 4.0.4.0 (`/data/ilert-dada-bt/stock/20-leaudio`):

- `ro.bluetooth.leaudio_offload.supported=true`,
  `persist.bluetooth.leaudio_offload.disabled=false`,
  `persist.vendor.qcom.bluetooth.aptx_adaptive_le.enabled=true`,
  `...lossless_aptx_adaptive_le.enabled=true`;
- `Codec location: ADSP`, PACS содержат LC3 и Qualcomm vendor codec
  `0xff/0x000a/0x0001` (aptX Adaptive LE / lossless);
- сток **тоже** отправляет `StartConnSubrate` с теми же параметрами
  (`max 2, min 1, cont 1, timeout 5000, mode 4`) и не падает;
- `persist.bluetooth.a2dp_offload.cap` в стоке **без** LC3
  (`sbc-aac-aptx-aptxhd-ldac-lhdc-mihc`), поэтому LC3 в A2DP-портах — наша
  ошибка конфигурации, а не фича;
- `lib_bt_ble.so`, `lib_bt_mihc.so`, `lib_bt_aptx.so`, `lib_bt_lhdc.so`,
  `lib_bt_bundle.so` в стоке есть; в нашем vendor-дереве они тоже уже есть.

Наш краш (`Hardware Error 0x0f`, `Rx VSE HW error::Bus error`) происходит
через ~350 мс после `StartConnSubrate`. Патч
`packages_modules_Bluetooth/0002-...` отключает host-initiated subrating
(по умолчанию; можно вернуть через
`persist.bluetooth.leaudio.subrating_disabled=false`). Патч
`0001-...` включает one CIS per channel для ADSP-оффлоада. Проп
`bluetooth.leaudio.dual_bidirection_swb.supported=true` выставлен как в
стоке.

Если после этого LE Audio всё ещё падает, следующий шаг — сравнить
`BTFM_PROXY`/`BTFMCODEC` и версию BT-прошивки с stock, а не трогать
`StartConnSubrate` дальше.

## Добавление нового патча

```bash
cd /data/AOSP/evo

# 1. Сделать изменение в проекте и закоммитить его локально.
# 2. Сгенерировать format-patch в каталог проекта:
git -C frameworks/av format-patch -1 --no-signature --zero-commit \
    --output-directory device/xiaomi/sm8750-common/patches/frameworks_av

# Если изменение ещё не закоммичено, можно сделать временный коммит,
# сгенерировать патч и откатить коммит:
git -C frameworks/av add -A
git -C frameworks/av -c user.name='ilert' -c user.email='ilert@local' \
    commit -m 'wip: local change'
git -C frameworks/av format-patch -1 --no-signature --zero-commit \
    --output-directory device/xiaomi/sm8750-common/patches/frameworks_av
git -C frameworks/av reset --soft HEAD~1
git -C frameworks/av reset
```

Номер должен быть следующим в каталоге проекта. Если путь проекта содержит
`_`, создайте рядом с патчами `.project` с настоящим путём:

```bash
printf '%s\n' 'external/rust/rutabaga_gfx' \
    > device/xiaomi/sm8750-common/patches/external_rust_rutabaga_gfx/.project
```

Проверка синтаксиса и заголовков:

```bash
bash -n device/xiaomi/sm8750-common/patches/apply-patches.sh
for p in device/xiaomi/sm8750-common/patches/*/*.patch; do
    sed '/^$/q' "$p" | grep -q '^From: .* <.*>$' || echo "no From: $p"
done
```

## Исключения

- `prebuilts/clang/host/linux-x86/clang-r614150/` — untracked toolchain,
  не патч; предупреждение libclang/Rust bindgen намеренное.
- `hardware/dolby` — остаётся форком, см. выше.
- `device/xiaomi/*`, `vendor/xiaomi/*`, `kernel/xiaomi/*` — device-specific
  репозитории, не форки upstream.
