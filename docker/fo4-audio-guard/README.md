# Fallout 4 audio failure guard

An optional F4SE plugin for **Fallout 4 1.10.163**. This complements Fluorine's
X3DAudio audio copy guard; it does not replace it. It is deliberately separate
from the XAudio DLLs because the affected functions belong to Fallout itself.

Fallout's source constructor leaves the wave-format storage uninitialized.
Source loading can set the buffered/streaming flags before parsing succeeds.
After a failed voice creation, `BSGameSound`'s status routine marks the sound as
failed but leaves its source flags intact. `ProcessSoundUpdates` can consequently
ask the failed source for its duration. The PCM duration calculation at
`Fallout4.exe+1AF5E06` divides by the source's block alignment without checking
for zero. Some crash symbol databases label this address as a destructor; it is
the duration getter at `+1AF5DB0`.

The guard clears the two readiness flags **only on a reported failure with an
invalid BSXAudio2DataSrc format**, preserving allocation ownership and all other
source fields. Both duration getters also return zero for invalid formats. Valid
sources use the original functions. Nothing repairs an invalid header or makes
voice creation falsely report success. Use the resulting logs to diagnose the
original asset/load failure; one identified cause and its repair are below.

F4SE's runtime check, the original status routine's full instruction sequence,
the duration prologues and both vtable targets must match before any change is
made. Another hook on these locations causes installation to be refused. Hooks
are installed during F4SE startup, not into a running game. No executable file is
modified. Remove the plugin to remove the guard on the next launch.

## Build and install

Portable releases include the DLL, this guide, and the two Python utilities
under `tools/fo4-audio-guard/`. They are optional and are not installed into games
automatically. Run the utilities with Python 3 from that directory, or use their
full paths. The source build instructions below are only needed when building
the DLL yourself.

With Fluorine's `fluorine-builder` image available, from the repository root:

```sh
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
  fluorine-builder bash docker/fo4-audio-guard/build.sh build/fo4-audio-guard
```

Install `FluorineAudioGuard.dll` under the game's `Data/F4SE/Plugins`, or at the
same relative path inside an enabled mod. Launch through F4SE. It needs neither
Address Library nor an extra C++ runtime DLL. Other Fallout runtimes are refused.

The companion `FluorineAudioGuard.sources.tsv` is optional. Generate it for the
current profile with `index-sources.py --help` and place it alongside the DLL.
The index reads loose filenames and BA2 directories without extracting media.
Rebuild it after changing mods. Logged matches are **candidates**, not proof of
which archive won load order; the resource ID and source pointer remain in the
log for correlation even without an index.

## Logs and validation

On Wine/Proton the default directory is
`$XDG_DATA_HOME/fluorine/logs/audio` or `~/.local/share/fluorine/logs/audio`.
`FLUORINE_AUDIO_LOG_DIR` overrides it, accepting an absolute Unix or Windows
path. Startup, installation refusal, sound-load failures and suppressed invalid
duration calculations are recorded in per-process `FluorineAudioGuard.log`
files. Records include UTC time, process/thread, caller RVA, resource ID,
source/format fields and matching filenames/providers. Each record is written
and its file closed immediately, so a later game crash does not lose stdio
buffers. Logs stay alongside the audio copy guard logs.

`test-proton.py` runs in a fresh prefix and accepts a locally supplied, unpacked
copy of the user's game image. No game image is distributed with the test.
It maps that image without running its entry point, imports or initialization,
then calls the real audio leaf functions with synthetic source objects. The
baseline must reproduce the exact `+1AF5E06` divide-by-zero. The patched run
checks PCM/WMA behavior, malformed formats, readiness/ownership preservation,
hook conflicts, F4SE ABI/version gating and persistent asset diagnostics.
This verifies the particular failure paths; a gameplay retest is still needed.

## Compressed streaming audio in rebuilt BA2 archives

One diagnosed installation had valid WAV/XWM payloads stored with BA2 zlib
compression. Fallout's failed loads had `parse_flags=0x2`, `audio_bytes=0` and
uninitialized wave-format fields. The affected files included vanilla UI,
ambience and effects in `Fallout4 - Sounds.ba2` and `Fallout4 - Startup.ba2`.
CLF3's CreateBSA handler had ignored Wabbajack's per-file `Compressed` settings
and used the BA2 builder's archive-wide compression default. The CLF3 fix passes
those original settings through and preserves stored entries in mixed archives.

WAV/XWM streaming audio should be stored without archive compression; FUZ voice
containers can remain compressed. This is also the distinction made by
[BSArch's smart compression](https://github.com/TES5Edit/TES5Edit/blob/dev-4.1.6/BSArch.dpr).
This issue is upstream of XAudio: preventing the invalid-format crash
does not restore sounds which failed to load.

`repair-ba2-audio.py` repairs explicitly selected **GNRL v1/v7/v8** archives.
Its default is a read-only audit:

```sh
python3 repair-ba2-audio.py \
  '/path/to/Data/Fallout4 - Sounds.ba2'
```

After closing Fallout, use `--apply --backup-dir /path/to/new-backup-directory`
with the same archive arguments. The tool backs up all selected originals,
stages and verifies every archive, then replaces files atomically. It verifies
decoded audio byte-for-byte and preserves all other entry bytes, names, flags
and compression settings. FUZ files are left alone. An installation error rolls
back files already replaced. `receipt.json` records original/installed archive
hashes, each changed entry's decoded hash, and backup paths for restoration.
The game executable and save files are not involved.

`test-repair.py` covers mixed stored/compressed archives, metadata and payload
preservation, corrupt input, backup/idempotence and rollback after a partial
installation failure. Existing installations need this repair (or an archive
rebuild); updating the CLF3 code alone cannot change their existing BA2 files.
