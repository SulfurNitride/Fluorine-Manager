# Wabbajack modlist compatibility audit

Audit date: 2026-09-04

Gallery input: `/home/luke/.cache/clf3/modlists/modlists.json` (219 entries)

## Executive summary

The gallery contains 141 entries with a GitHub-backed readme or website, across 125 repositories. This audit fetched the linked text documents (raw GitHub URLs where possible) and used shallow, blob-filtered clones only for a small set of high-value repositories. No application source was changed.

Important correction after local validation: an upstream README saying Linux is
"unsupported" is a support-policy statement, not proof that the list fails in
Fluorine. CSVO, Wasteland of Depravity, Merethic, and Apostasy are known to work
and must not be hidden or marked incompatible. This audit is used to discover
manual post-install work only; it is not a compatibility deny-list.

Adapter candidates must also show public repository activity or a verifiable
public modlist release within the prior 12 months. For this audit the cutoff is
2025-09-04. A gallery entry alone is not maintenance evidence. Lists outside
that window require an explicit, reviewed exception based on current knowledge.

For the Bethesda-focused subset (122 entries: Fallout, Skyrim, Oblivion, Morrowind, Starfield), 121 documents fetched successfully and one failed (`OddlyMistaken/Dying-Breath`'s default `README.md`, HTTP 404; repository HEAD was still available). The subset covers 110 repositories; all 122 entries had a resolvable repository HEAD via `git ls-remote`.

Signal counts in those 122 documents are intentionally broad text matches, not proof that every occurrence is a user action:

| Signal | Entries |
| --- | ---: |
| post-install heading/text | 53 |
| manual install | 8 |
| Root Builder | 12 |
| Linux | 17 |
| Proton | 7 |
| patcher/patching | 11 |
| SKSE | 36 |
| BSA | 22 |
| ENB | 42 |
| ReShade | 14 |
| page file | 10 |
| shader cache | 17 |

## Implemented adapter status (2026-09-04)

- `VivaNewVegas`: reviewed manual-root deployment into the original game folder
  and the native Linux/Proton FNV 4 GB patcher resolved from Nexus
  mod 62552. Premium accounts use the API link; regular accounts use Fluorine's
  embedded Nexus authorization browser.
- `heartlandredux`: reviewed `Game Folder Files` deployment into the original
  game folder and Nexus mod 56555's native Linux 4 GB patcher for
  `Oblivion.exe` and `OblivionLauncher.exe`, with automatic `.Backup` files and
  independent post-write PE-header verification. Premium accounts use the API
  link; regular accounts use Fluorine's embedded Nexus authorization browser.
- `watersoflife`: reviewed `__Files Requiring Manual Install` deployment into
  the original game folder. The supplied Fallout
  Anniversary Patcher is still presented as a required warning until its
  noninteractive Proton completion can be verified reliably.
- `Morrowind75` remains an explicitly disabled adapter because its multi-stage
  interactive first-run wizard cannot be guessed safely.
- `fotw` was removed from the runtime catalog after the maintenance-window check
  and confirmation that the list is abandoned. Its generic BSA operations
  remain reusable for maintained lists.

Fluorine does not synthesize Stock Games. It preserves the modlist author's
layout: manual game-root files go to the original game folder, while a Stock
Game is used only when the modlist itself authored and installed one.

## Strong adapter candidates

These have concrete, bounded actions that Fluorine can expose as reviewed recipes. “Automatable” means the filesystem/configuration part can be performed after explicit user confirmation; game launch, first-run choices, Nexus authentication, and in-game MCM choices remain user-facing.

### Waters of Life (Fallout 3) — highest-value patcher candidate

- Source: [`zpok3/Waters-of-Life` README](https://github.com/zpok3/Waters-of-Life/blob/main/README.md), Linux guide [`Linux-Guide.md`](https://github.com/zpok3/Waters-of-Life/blob/main/Linux-Guide.md)
- Gallery machine name: `watersoflife`
- Audited repository HEAD: `24ef7d4f7641c9c24238ef1a83f3628b72413643`
- README sections: `Post Installation` line 166, `Root Mods` line 177, `BSA Decompressor` line 190.
- Required actions: copy `__Files Requiring Manual Install` into the game root; run `Patcher.exe`; verify `Fallout3_backup.exe`; optionally run the BSA decompressor or write its output to an MO2 mod; launch the game directly after patching.
- Linux guide explicitly points to Omni's Proton/Wine guides, SteamTinkerLaunch, `protontricks`, dot-file visibility, and components `xact`, `xact_x64`, `d3dcompiler_47`, `d3dx11_43`, `d3dcompiler_43`, `vcrun2022`, and `fontsmooth=rgb`.
- Classification: root-file staging and post-patch verification are automatable; selecting a BSA output directory is confirmable; patcher/BSA execution should use the list's supplied Linux-compatible binary when present, otherwise Proton/Wine; first-run/INI troubleshooting is user-only.

### The SICKnasty Suite (Fallout 4) — bounded generated-patch recipe

- Source: [`KCisSICKnasty/SICKnasty-FO4` README](https://github.com/KCisSICKnasty/SICKnasty-FO4/blob/main/README.md)
- Machine name: `TheSickNastySuite`
- HEAD: `697ce1be27c770c13a941e2376002c77955a6f7f`
- README lines 116–128 describe the Fallout 4 DLC Consistency Patch: copy matching vanilla files with `.vcdiff` into the patch `Data` folder, run `XD3Patcher`, remove optional intermediates, zip the generated files, install the result in MO2, and place it early in the load order.
- Classification: strongly automatable if Fluorine can identify the list's game root and generated output mod; validate that all expected `.vcdiff` inputs exist and that output contains no backup/intermediate files. High-FPS INI tuning is hardware-dependent and user-only. Windows Defender/high-DPI directions are irrelevant on Linux.

### High & Dry (Fallout: New Vegas) — BSA recipe

- Source: [`Jayer117-ball/HighandDry` README](https://github.com/Jayer117-ball/HighandDry/blob/main/README.md)
- Machine name: `highanddry`
- HEAD: `fab9808d854645e0b43877a98ef622ef71c60b7a`
- README lines 312 and 357 contain post-install and BSA sections; line 361 names `FNV BSA Decompressor.exe`.
- Classification: bounded optional BSA generation with explicit input/output
  validation; no Linux-specific instruction was found in this README.

### A Painted World (Oblivion) — Root Builder/Stock Game recipe

- Source: [`sasquatch678/A-Painted-World` README](https://github.com/sasquatch678/A-Painted-World/blob/main/README.md)
- Machine name: `APaintedWorld`
- HEAD: `316c0bf608002776d959d0f7e4be28258acdae1f`
- README lines 131–153 require resolution configuration and optional Vulkan ReShade installation; lines 189–193 document Stock Game and Root Builder for root hooks.
- Classification: profile-resolution edits, Stock Game detection, and Root Builder validation are automatable. ReShade binary installation and DXVK choice should be explicit opt-ins; Windows antivirus directions are irrelevant on Linux.

### Deckborn (Skyrim SE) — explicit Steam Deck recipe

- Source: [`Pentonize/DeckBorn` README](https://github.com/Pentonize/DeckBorn/blob/main/README.md)
- Machine name: `Deckborn`
- HEAD: `861ad3edb9075af2e1ccdd036f54a850b4175d17`
- README lines 90–121 configure a non-Steam shortcut, Proton Experimental, `SteamDeck=0`, optional SD-card mount, Protontricks/Flatseal permissions, and a Protontricks alias.
- Lines 143–154 install `xact`, `xact_x64`, `d3dcompiler_47`, `d3dx11_43`, `d3dcompiler_43`, `vcrun2022`, `dotnet6`, and `dotnet7` into the prefix.
- Lines 138–142 configure MO2 to launch `skse64_loader.exe` from the list's `mods/SKSE/Root` path.
- Classification: useful recipe source for Fluorine's generic Steam Deck/Proton setup. Do not copy the shell command verbatim: resolve the prefix/app ID and filesystem paths safely, then validate each installed component. Retained through an explicit maintenance exception confirmed on 2026-09-04.

### ASSOS (Skyrim SE) — Linux-developed list

- Source: [`The-Animonculory/ASSOS` Readme](https://github.com/The-Animonculory/ASSOS/blob/master/Readme.md)
- Machine name: `ASSOS`
- HEAD: `38a3841539c361e6b4e741b13f0d3d88db0092ed`
- README lines 71–73 explicitly state that the list works on Linux because it is developed on Linux, while directing users to Jackify; line 153 starts post-installation/BethINI guidance.
- Classification: strong compatibility signal and validation target, but the documentation does not itself define a large list-specific patch sequence. Use it to test generic Fluorine setup and launcher handling.

### Tuxborn (Skyrim SE) — first-party Linux documentation

- Source: [`Omni-guides/Tuxborn` README](https://github.com/Omni-guides/Tuxborn/blob/main/README.md), [`afterinstall.md`](https://github.com/Omni-guides/Tuxborn/blob/main/afterinstall.md)
- Machine name: `Tuxborn`
- HEAD: `15d6590a0cc04b2104c7154d79ec3a17a3ab855f`
- README says Linux/Steam Deck installation uses Jackify. The repository also contains `LinuxInstall.md` and `LinuxNexusPremium.md` (both currently short redirects to `afterinstall.md`). `afterinstall.md` line 142 says the xEdit Pronouns Patcher is already done, but users must set fallback pronouns in the in-game MCM.
- Classification: generic Linux installer plus explicit “user-only MCM” state. Fluorine should not claim the recipe is complete until the MCM setting is acknowledged.

## Reusable upstream Linux implementation

The most useful “separate agent” result is already an upstream, reviewable implementation rather than arbitrary README command execution:

- Repository: [`Omni-guides/Wabbajack-Modlist-Linux`](https://github.com/Omni-guides/Wabbajack-Modlist-Linux)
- Audited HEAD: `7f61d9103fded6906d4d216b049f689ea4ff258e`
- `Legacy/binaries/omni-guides-fnvfix.sh` and `omni-guides.sh` are Linux/Steam Deck post-install scripts. They detect native versus Flatpak Protontricks, grant Flatpak filesystem permissions, enable Wine dot files, install per-game components, and validate installation.
- The scripts currently map common components as follows: Skyrim SE/Fallout 4 → `d3dcompiler_47 d3dx11_43 d3dcompiler_43 dotnet6 dotnet7`; Fallout New Vegas → `d3dx9_43 d3dx9` with forced Steam app ID `22380`; Fallout 3 is covered by the Waters of Life guide's explicit Protontricks set.
- `Legacy/binaries/WabbajackProton.sh` provides prefix/app-ID discovery, WebView setup, Protontricks detection, and compatdata handling.
- `Legacy/binaries/WabbajackWine.sh` is an experimental Wine setup path and should not be silently preferred over Proton on Steam installations.
- The repository's `Legacy/README.md` links supported list-specific Linux guides for Welcome to Paradise, Begin Again, Tuxborn, Anvil, Nordic Souls, Legends of the Frost, and others.

Recommended Fluorine integration: vendor a reviewed, version-pinned subset of the upstream logic as declarative operations (prefix setup, component install, root staging, patch execution, verification). Track the upstream commit and expose a “review upstream changes” notification. Do not execute arbitrary shell snippets extracted from list READMEs.

The upstream scripts are useful reference implementations, not drop-in Fluorine helpers: the current shell code can install Flatpaks, append aliases to a user's shell startup file, change Flatpak filesystem permissions, and terminate broad process-name matches during cleanup. Each such operation needs a scoped, consented equivalent in Fluorine.

## Other notable records

| List | Game | HEAD | Findings | Classification |
| --- | --- | --- | --- | --- |
| Sim Settlements 2 City Plan Contest Helper | Fallout 4 | `5f362b2b76de967bd3d602842b545ab187d8c9fc` | `Post-Installation`; launch F4SE through MO2, wait for scripts, save; ENB/Steam overlay notes | Launch/profile validation automatable; first save user-only |
| Plotapalooza Contest Helper | Fallout 4 | `5441d01019982c080721469ebcba46e0053af404` | Same Yagisan post-install pattern; launch F4SE through MO2 | Same adapter as SS2CPC |
| Waters of Life | Fallout 3 | `24ef7d4f7641c9c24238ef1a83f3628b72413643` | Root patcher, BSA decompressor, Linux guide | Strong adapter; see above |
| LoreOut | Fallout 4 | `f801c7d042a173af80a9c4603be6c39501f97037` | Linux mentioned as lower-RAM context but explicitly not officially supported | Warning only; no compatibility claim |
| Wasteland of Depravity | Fallout 4 | `908487a2f7eac32bfc84f93b2c44f4d2d198c5a3` | README declines Linux support; locally confirmed working | Do not mark incompatible; automate only concrete manual steps |
| Cosmoem | Fallout 4 | `29edd2c385623ceabb01443609e1c872dc8355e1` | Post-install; Windows NVIDIA shader-cache UI; F4SE | Shader-cache UI irrelevant on Linux; generic launch checks |
| Magnum Opus | Fallout 4 | `e1e9bf6f1fc0cf3f4dbeeafc18bd12797c2ccbc0` | Post Installation points to separate importance/setup page | Follow linked page before adapter work |
| Merethic | Skyrim SE | `950b08e16a1971842de4faf2c6a56cafc1640d54` | README declines Linux support; locally confirmed working; post-install and manual downloads | Working; audit only concrete manual steps |
| Alpyne | Skyrim SE | `3bba74840195e04da933d66e016cf89859ad1b2d` | Explicit Linux + Proton requirement and Jackify link | Generic Linux adapter candidate |
| Ainulindale | Skyrim SE | `93f3f812f82195ce1dbdb1a9ba526d3c4799245f` | PGPatcher/ParallaxGen must be run after install; output generated into MO2 mod | Strong bounded patcher recipe; user confirms texture sorting/settings |
| CSVO | Skyrim SE | `5a199e899cb7ef8acdaf55edec76bac4ee42fb33` | README declines Linux support; locally confirmed working; LOD/PGPatcher post-install | Working; patcher follow-up only |
| Elysium Remastered | Skyrim SE | `d01fde292576356fa73e16d99c6c71334c7d763f` | Post-install, Root Builder, ENB/ReShade, Proton mention | Candidate after root-hook validation |
| Apostasy | Skyrim SE | `b934cadfb449d2acd9217da705f8607e4f06a3d3` | README declines Linux support; locally confirmed working; shader cache and BSA notes | Working; audit only concrete manual steps |
| Deckborn | Skyrim SE | `861ad3edb9075af2e1ccdd036f54a850b4175d17` | Detailed Proton/Protontricks path | Strong adapter; maintenance exception confirmed 2026-09-04 |
| Tuxborn | Skyrim SE | `15d6590a0cc04b2104c7154d79ec3a17a3ab855f` | Jackify Linux path, after-install/MCM | Strong adapter |

## Maintenance-window exclusions

These entries had useful technical instructions, but no verified activity on or
after the 2025-09-04 cutoff and no reviewed exception. They are retained only as
audit history and must not appear as active or upcoming Fluorine adapters.

| List | Last repository activity | Runtime effect |
| --- | --- | --- |
| Fear of the Wanderer | 2024-11-07 | Removed disabled `fotw` profile and candidate |

Its generic implementation work is not discarded: maintained lists may reuse
the reviewed BSA operations.

### Reviewed maintenance exceptions

Repository age is a warning signal rather than an automatic compatibility
verdict. Heartland Redux and Deckborn remain in scope following explicit current
confirmation on 2026-09-04.

| List | Last repository activity | Status |
| --- | --- | --- |
| Heartland Redux | 2024-05-27 | Retain implemented adapter |
| Deckborn | 2024-12-13 | Retain strong adapter candidate |

## Classification rules for Fluorine

Automate only deterministic operations with a known target and validation: copy a named `Files Requiring Manual Install` directory, create a Root Builder/Stock Game mapping, install a known Protontricks component set, run a named patcher with a known input/output contract, edit a profile INI key, and verify a backup/output file.

Keep user-only: Nexus/Premium authentication and manual download clicks; first launch and script warm-up; character/new-game setup; MCM choices (including Tuxborn pronouns); hardware-specific resolution/FPS tuning; deciding whether to enable ENB/ReShade/DXVK; and any step requiring visual judgment.

Ignore or translate on Linux: Windows Defender exclusions, Windows high-DPI dialogs, Windows page-file instructions, NVIDIA Control Panel shader-cache instructions, Steam Windows UI wording, and direct `double-click` instructions. Their intent may still map to a Linux validation or Proton task, but the Windows procedure must not be shown as-is.

Never auto-execute arbitrary README commands, downloaded scripts, or unknown `.exe` files. Require a reviewed adapter, pinned source commit, explicit user confirmation, sandbox/prefix selection, and postcondition checks. For patchers, prefer a supplied Linux executable/script when the list provides one; otherwise run a known Windows patcher through the list's Proton prefix only after confirming its input/output contract.

## Failure and coverage notes

- 19 gallery entries are outside the Bethesda-focused subset and were not deeply classified here; the inventory should still retain their source URLs for a later pass.
- One of 122 Bethesda-scoped raw documents failed: `OddlyMistaken/Dying-Breath` repository HEAD resolved (`96555700d2cecf7bfa53ae967dd880c4aab1580a`) but its guessed default README path returned HTTP 404. Use the repository tree or its linked documentation on the next pass.
- GitHub HTML pages were converted to raw URLs when the branch/path was explicit. Wiki URLs and external non-GitHub pages were not cloned; their presence was recorded as a follow-up rather than treated as README content.
- A repository HEAD is a reproducibility anchor, not necessarily the commit containing a specific README path when a list links a wiki or moving external documentation. Inventory records should store both the source URL and fetched-content SHA-256 when building the production importer.
