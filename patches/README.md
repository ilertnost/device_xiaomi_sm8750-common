# Evolution X patches for the Xiaomi 15 (dada)

Since 2026-11-22 upstream projects are no longer forked. The tree carries
stock AOSP / Evolution X / LineageOS, and all local changes live in
`device/xiaomi/sm8750-common/patches` and are applied by a script. Only
device-specific repositories (device tree, vendor blobs, kernel) and
`hardware/dolby` (HyperOS blobs) remain forks.

The layout follows `android_device_xiaomi_dada`:
`device/xiaomi/dada/vendorsetup.sh` calls
`device/xiaomi/sm8750-common/patches/apply-patches.sh` on
`source build/envsetup.sh`.

## Layout

```
device/xiaomi/sm8750-common/patches/
    <project-path-with-underscores>/
        NNNN-description.patch
```

The directory name is the project path from the tree root with `/` replaced
by `_`. For example:

```
frameworks_av/                              -> frameworks/av
frameworks_native/                          -> frameworks/native
external_crosvm/                            -> external/crosvm
external_rust_rutabaga_gfx/                 -> external/rust/rutabaga_gfx
packages_modules_Bluetooth/                 -> packages/modules/Bluetooth
packages_modules_Virtualization/            -> packages/modules/Virtualization
hardware_xiaomi/                            -> hardware/xiaomi
hardware_qcom-caf_sm8750_display/           -> hardware/qcom-caf/sm8750/display
vendor_qcom_opensource_display-core/        -> vendor/qcom/opensource/display-core
vendor_qcom_opensource_display-intf/        -> vendor/qcom/opensource/display-intf
vendor_qcom_opensource_interfaces/          -> vendor/qcom/opensource/interfaces
```

If the project path itself contains an underscore
(`external/rust/rutabaga_gfx`), replacing `/` with `_` is not enough. Such a
directory carries a `.project` file with the real path:

```
external_rust_rutabaga_gfx/.project   -> external/rust/rutabaga_gfx
```

Patches for one project are applied in file name order (`0001`, `0002`, ...).
The script requires `git format-patch` output (a `From: ... <...>` line);
plain diffs are skipped.

Directories starting with `_` are ignored by the script:

- `_committed/` — old device/vendor patches whose changes are already
  committed in their own repositories;
- `_optional/` — experimental patches (classic aptX Adaptive);
- `_forked-blob-repos/` — patch backups for repositories that remain forks
  (for example `hardware/dolby`).

## Applying the patches

Automatically:

```bash
cd /data/AOSP/evo
source build/envsetup.sh
```

`vendorsetup.sh` runs the script only when
`.repo/local_manifests/dada.xml` carries the patch-based manifest header.
With the old fork-based manifest the patches are already part of the fork
branches, so automatic application is disabled to avoid applying them twice.

Manually:

```bash
cd /data/AOSP/evo
bash device/xiaomi/sm8750-common/patches/apply-patches.sh \
    device/xiaomi/sm8750-common/patches
```

How the script works:

- for every project it takes the base commit from `refs/remotes/m/` (the
  manifest revision);
- it applies all patches of the project in a temporary index and compares
  the result with the working tree;
- if the tree already matches, it does nothing;
- if the tree is clean and sits on the base, it applies the patches with
  `git am`, preserving the author and date;
- if there are local changes, an unfinished am/rebase/merge, or similar, it
  reports the project and leaves it untouched.

## Fork-free manifest

`device/xiaomi/sm8750-common/patches/dada-patches.xml` is a variant of
`.repo/local_manifests/dada.xml` where upstream projects are returned to
their owners and pinned to the commits the patches were generated against:

| Project | Owner | Patch base |
|---|---|---|
| `frameworks/av` | Evolution-X | `80917b1522` |
| `frameworks/native` | Evolution-X | `8c23b549b2` |
| `external/crosvm` | AOSP | `c7dd8d6950` |
| `external/rust/rutabaga_gfx` | AOSP | `1f181b0b3e` |
| `packages/modules/Virtualization` | AOSP | `22f1c9ee92` |
| `hardware/xiaomi` | LineageOS | `fb0a2d51c7` |
| `hardware/qcom-caf/sm8750/display` | LineageOS | `5b970807ee` |
| `vendor/qcom/opensource/display-core` | LineageOS | `89aa1914da` |
| `vendor/qcom/opensource/display-intf` | LineageOS | `19b5c055b4` |
| `packages/modules/Bluetooth` | Evolution-X | `00f2fc1131` |
| `vendor/qcom/opensource/interfaces` | LineageOS | `b75ec20e94` |

Switching (after the patches have been verified):

```bash
cd /data/AOSP/evo
mv .repo/local_manifests/dada.xml .repo/local_manifests/dada.xml.bak
cp device/xiaomi/sm8750-common/patches/dada-patches.xml \
   .repo/local_manifests/dada.xml
repo sync -j4
source build/envsetup.sh          # applies the patches automatically
```

Do not place `dada-patches.xml` next to `dada.xml`: `repo` reads every
`*.xml` in `local_manifests/` and would define the same projects twice.

Device/vendor changes are already committed locally and are not patches:

- `device/xiaomi/sm8750-common` — `c9e0c3f` (BAP, `dual_bidirection_swb`,
  subrating disabled);
- `vendor/xiaomi/dada` — `d356cdb` (LC3 removed from the A2DP ports).

These commits have not been pushed yet. Their old patch files are kept only
as backups under `_committed/`.

`hardware/dolby` remains an `ilertnost` fork in the new manifest because it
carries HyperOS blobs. Its patches live under
`_forked-blob-repos/hardware/dolby/` and are not applied by default.

## Patch list

| Project | Patches | Purpose |
|---|---:|---|
| `frameworks/av` | 1 | camera: implementation-defined stream sizes |
| `frameworks/native` | 1 | SF: do not protect the client target for protected camera |
| `external/crosvm` | 2 | Android.bp visibility for vendor/platform |
| `external/rust/rutabaga_gfx` | 1 | Android.bp visibility |
| `packages/modules/Virtualization` | 1 | display client AIDL for the platform |
| `hardware/xiaomi` | 10 | fingerprint, MiSys, IR, powershare, sensors, touchfeature, Euicc |
| `hardware/qcom-caf/sm8750/display` | 3 | getDisplayAttributes, xiaomi_rgb_notifier, panel size |
| `vendor/qcom/opensource/display-core` | 3 | getDisplayAttributes, light sensor, panel size |
| `vendor/qcom/opensource/display-intf` | 1 | display attributes by hardware composer |
| `packages/modules/Bluetooth` | 2 | one CIS per channel; subrating disabled |
| `vendor/qcom/opensource/interfaces` | 1 | `codecSpecific` in classicVendorConfiguration |

26 active patches in total. Backups: 2 `hardware/dolby` patches, 2 old
device/vendor patches under `_committed/`, and 1 optional classic aptX
patch.

## Optional: classic aptX Adaptive (WIP)

`_optional/packages/modules/Bluetooth/0001-classic-aptx-adaptive-wip.patch`
is the saved net diff of the `backup/bt-local-20261121` branch (aptX
Adaptive port from AOSPA + QTI `classicVendorConfiguration` + AVDT/AIDL
fixes). The script does not apply it: classic A2DP aptX AD previously
triggered `Hardware Error 0x0f`, while the current stable stack is SBC/AAC.
Apply it manually only if classic aptX AD is needed:

```bash
git -C packages/modules/Bluetooth apply \
    device/xiaomi/sm8750-common/patches/_optional/packages/modules/Bluetooth/0001-classic-aptx-adaptive-wip.patch
```

Important: **LE** aptX Adaptive (`0xff/0x000a/0x0001`) is not supported by
the stock Evolution X Bluetooth module.
`system/bta/le_audio/le_audio_vendor_codec.h` only knows the Google vendor
codec `0x00e0`; the Qualcomm `0x000a` codec is absent from the backup branch
as well. To make the earbuds switch from LC3 to aptX Adaptive LE, a separate
port of the Qualcomm vendor codec is required in
`le_audio_vendor_codec.h`/`codec_manager.cc` (stock HyperOS has this
support).

## LE Audio: findings from the stock capture

Stock HyperOS 4.0.4.0 (`/data/ilert-dada-bt/stock/20-leaudio`):

- `ro.bluetooth.leaudio_offload.supported=true`,
  `persist.bluetooth.leaudio_offload.disabled=false`,
  `persist.vendor.qcom.bluetooth.aptx_adaptive_le.enabled=true`,
  `...lossless_aptx_adaptive_le.enabled=true`;
- `Codec location: ADSP`; PACS contains LC3 and the Qualcomm vendor codec
  `0xff/0x000a/0x0001` (aptX Adaptive LE / lossless);
- stock **also** sends `StartConnSubrate` with the same parameters
  (`max 2, min 1, cont 1, timeout 5000, mode 4`) and does not crash;
- stock `persist.bluetooth.a2dp_offload.cap` has **no** LC3
  (`sbc-aac-aptx-aptxhd-ldac-lhdc-mihc`), so LC3 in the A2DP ports was our
  configuration error, not a feature;
- `lib_bt_ble.so`, `lib_bt_mihc.so`, `lib_bt_aptx.so`, `lib_bt_lhdc.so`,
  and `lib_bt_bundle.so` are present in stock; our vendor tree already has
  them too.

Our crash (`Hardware Error 0x0f`, `Rx VSE HW error::Bus error`) happens
about 350 ms after `StartConnSubrate`.
`packages_modules_Bluetooth/0002-...` disables host-initiated subrating by
default; it can be re-enabled with
`persist.bluetooth.leaudio.subrating_disabled=false`. Patch `0001-...`
enables one CIS per channel for the ADSP offload path. The property
`bluetooth.leaudio.dual_bidirection_swb.supported=true` is set as in stock.

If LE Audio still crashes after that, the next step is to compare
`BTFM_PROXY`/`BTFMCODEC` and the Bluetooth firmware version with stock
instead of changing `StartConnSubrate` further.

## Adding a new patch

```bash
cd /data/AOSP/evo

# 1. Make the change in the project and commit it locally.
# 2. Generate a format-patch into the project directory:
git -C frameworks/av format-patch -1 --no-signature --zero-commit \
    --output-directory device/xiaomi/sm8750-common/patches/frameworks_av

# If the change is not committed yet, create a temporary commit,
# generate the patch, and undo the commit:
git -C frameworks/av add -A
git -C frameworks/av -c user.name='ilert' -c user.email='ilert@local' \
    commit -m 'wip: local change'
git -C frameworks/av format-patch -1 --no-signature --zero-commit \
    --output-directory device/xiaomi/sm8750-common/patches/frameworks_av
git -C frameworks/av reset --soft HEAD~1
git -C frameworks/av reset
```

The number must be the next one in the project directory. If the project
path contains `_`, create a `.project` file next to the patches with the
real path:

```bash
printf '%s\n' 'external/rust/rutabaga_gfx' \
    > device/xiaomi/sm8750-common/patches/external_rust_rutabaga_gfx/.project
```

Syntax and header check:

```bash
bash -n device/xiaomi/sm8750-common/patches/apply-patches.sh
for p in device/xiaomi/sm8750-common/patches/*/*.patch; do
    sed '/^$/q' "$p" | grep -q '^From: .* <.*>$' || echo "no From: $p"
done
```

## Exceptions

- `prebuilts/clang/host/linux-x86/clang-r614150/` is an untracked toolchain,
  not a patch; the libclang/Rust bindgen warning is intentional.
- `hardware/dolby` remains a fork, see above.
- `device/xiaomi/*`, `vendor/xiaomi/*`, and `kernel/xiaomi/*` are
  device-specific repositories, not upstream forks.
