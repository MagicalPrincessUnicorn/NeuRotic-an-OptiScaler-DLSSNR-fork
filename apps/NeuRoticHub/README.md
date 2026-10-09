# NeuRotic desktop App

The portable native x64 App selects games, installs/removes NeuRotic, edits saved game settings and runs NR Anything. Keep its complete support/package/discovery folders together. Start `NeuRotic.exe`.

## Game installation

Install also updates an existing installation. Fresh Install resets package INI defaults. Uninstall removes recorded owned files independently, including modified files; missing files count as removed. A failed/locked file leaves a Partial receipt for retry. Foreign files offer Replace, Skip or Cancel. Keep ReShade is an explicit coexistence choice; NeuRotic does not install ReShade. No backups, restore/history, Repair, Sanitize or Dumbfire actions belong to supported game maintenance.

The relative schema3 receipt travels with a moved game. Character Inspector is installed under `OptiScaler/CharacterInspector/Version 1` and reused when that version folder exists. Activation stays optional. Unknown files and older versions are not swept during update. Packaging/runtime cohort integrity still applies.

Game selection reads bounded metadata and editable INI fields rather than hashing installed payloads. Anti-cheat findings require acknowledgment for affected writes. This is warning evidence, not assurance of compatibility. Incompatible in-game architecture is refused before proxy writes; use NR Anything. No 32-bit replacement or third-party compatibility wrapper installation is supplied.

## Language and NR Anything

Settings provides one shared Language system for the App and next-launch in-game menu. English is always available. Nine included partial draft packs cover zh-CN, ru, es, pt-BR, de, ja, fr, pl and ko. Valid stale entries remain above English and show Needs review; native-speaker review is pending.

Manage Languages supports Add Language, Import, Export English Template, edited-pack sharing Export and an owned second-window Translation Editor. Save Draft keeps edits inactive. Apply publishes validated entries. The editor previews controls with synthetic state, preserves drafts across imports and reviews dirty closure. Packs contain data only and cannot supply fonts, paths or executable actions. Pack/draft/active data live once in LocalAppData/NeuRotic/HubData-v2/Languages; no per-game duplication. Both surfaces embed licensed font fallbacks. Other locales can be created without claiming shaping/RTL support.

NR Anything keeps processing and target information together. Countdown and clear-target X are in Targeted Window; the main action uses bold readable text. A missing NR model opens its dedicated placement folder. DepthAnything remains excluded. NR output and useful displayed FG ratios still require hardware/game qualification.

## Build and verification

`tools/hub/Build-Native.cmd` builds the App in its stable cache. `Build-Hub.ps1 -NativeOnly -SupportOnly` builds/stages support without changing package approval. Full staging requires a current verified complete native package via PackageRoot. `tools/hub/Test-Hub.ps1 -Suite All` exercises the supported fixture suite, not real games.

`tools/packaging/customer/Build-ReleaseBundle.py --verify-only` requires exact frozen-source renderer/component receipts and verifies the complete assembly without a ZIP. `Package-Hub.py` delegates to this builder. Distribution is a separately requested operation. CPU Inspector, forwarder pairing, module architecture, hashes and embedded font/license resources remain checked.

Real-game installs/launches, signatures, publishing, clean-machine/UAC and broad hardware qualification are outside these local fixtures. See the overnight implementation report for exact identities, rendered evidence and remaining Detroit/Control tests.
