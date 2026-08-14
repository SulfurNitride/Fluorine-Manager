# Experimental USVFS backend

Fluorine can use either its native FUSE filesystem or USVFS for executables
launched through Wine/Proton. The choice is stored per instance under
`fluorine/vfs_backend` and is available in **Settings > Wine/Proton > VFS**.
FUSE remains the default.

## Selection rules

| Launch | FUSE selected | USVFS selected |
| --- | --- | --- |
| Windows executable through Proton | FUSE | USVFS |
| Native Linux executable | FUSE | FUSE |
| Game with `usesVFS=false` (for example OpenMW) | Game-managed VFS | Game-managed VFS |

There is deliberately no automatic fallback from USVFS to FUSE. A missing or
broken USVFS runtime produces a launch error so benchmarks and bug reports do
not silently measure a different backend.

## What is actually implemented

The Fluorine worktree implements the integration layer, not a wholesale USVFS
rewrite:

- a per-instance FUSE/USVFS selector and safe FUSE default;
- reuse of Fluorine's ordered file mappings, create-target flags, forced DLLs,
  executable blacklist and file/directory skip lists;
- a database-backed resolved snapshot path that reuses the FUSE catalog's
  fingerprint hits/misses and sends the winning Data file map to USVFS in one
  bulk request;
- a versioned binary request serializer and bounded Wine-side parser;
- a statically linked Windows controller that creates USVFS, installs mappings,
  launches the target through `usvfsCreateProcessHooked`, drains registered
  children and disconnects cleanly;
- Proton and Steam Linux Runtime plumbing for the helper, request and runtime;
- script-extender lifetime tracking, failure cleanup, Root Builder preparation
  and delayed post-run INI/plugin synchronization;
- pinned/hash-verified USVFS packaging, content-derived portable bundle
  identity, structured timing logs and the repeatable True North harness.

The normal packaged DLLs come from the fork's short-name-only release. A
provenance-recorded experimental DLL may additionally contain independently
gated candidates. `fluorine/usvfs_exact_query_exhaustion` controls the
exact-name directory-query shortcut and `fluorine/usvfs_shared_context`
controls the recursive reader/writer context lock; both are per-instance and
default to false. Fluorine passes their normalized `0`/`1` values to Wine as
`FLUORINE_USVFS_EXACT_QUERY_EXHAUSTION` and
`FLUORINE_USVFS_SHARED_CONTEXT`. The benchmark harness may override them for
one launch without editing the instance INI.

The original both-on diagnostic artifact reproduced a mixed-season asset
regression even though each gate was visually correct alone. The interaction
was traced to unprotected mutable directory-search continuation state. Commit
`d6e1186` serialized that lifecycle with a process-wide search mutex and passed
CI, the four-way 100K workload and the headless True North/Root Builder gate,
but a normal launch did not produce a usable window. It is rejected. Commit
`12b5e3f` replaces the global mutex with lifetime-safe per-handle records and
passed CI, the four-way 100K workload, warmed control/both-on repeats, and a
strengthened headless test that observed a real Gamescope render surface. A
normal visible launch then reproduced the same mixed/incorrect winter textures
with both gates enabled. The user later identified a mod under active
development as the likely source of that asset mixture, so the visual run is
confounded and no longer rejects `12b5e3f`. Both options remain experimental
and off pending a clean fixed-mod-state A/B. Disabled logging, the recursive-benaphore
exclusion repair, and the
wide-filename cleanup remain separate narrow/correctness changes; the pinned
short-name-only release does not contain those later fork changes. The
retained `687e890` diagnostic DLL does contain them, plus both
disabled experimental implementations; it is not the short-name-only
payload. Packed
directory results, serving exact/negative caches and precomputed listings are
not enabled because their Windows directory-query ABI and cross-process
invalidation prerequisites are not yet complete.

## Runtime design

The native Fluorine process cannot load a Windows DLL. For an USVFS launch it
therefore writes a temporary, versioned, length-prefixed request containing:

- the target executable, working directory and already-tokenized arguments;
- the same ordered mappings used by the existing FUSE connector;
- when the DLL supports it, a deterministic resolved Data snapshot containing
  the winning physical source for each virtual file and its parent directories;
- write-target flags, forced libraries, and skip lists;
- a unique USVFS instance name and an output log path.

Proton starts `usvfs/fluorine-usvfs-launcher.exe` instead of the requested
program. That Windows helper loads `usvfs_x64.dll`, installs the mappings, and
starts the actual target with `usvfsCreateProcessHooked`. The helper remains
alive until the target and its registered USVFS children exit, then disconnects
the VFS and returns the target's exit code. This is one Proton/Steam Linux
Runtime launch; Proton is not nested inside another Proton process.

The request is deleted by the helper immediately after successful parsing.
Fluorine removes it itself if validation or process creation fails. The parser
rejects bad magic, unsupported versions, truncated input, strings over 16 MiB,
excessive entry counts, invalid UTF-8 and trailing data.

The request normally appears under `/tmp` because it is ephemeral IPC, not a
benchmark log. Proton accesses the host file through Wine's `Z:` mapping, and
the Steam Linux Runtime is granted that exact filesystem location. Keeping it
temporary avoids retaining a binary copy of every mod mapping/path in the
instance. Durable benchmark records and helper logs belong under the instance's
`logs/benchmarks` and `logs` directories; the harness verifies that the request
itself has been removed.

Fluorine uses the helper, rather than the initially requested executable, as
the process-lifetime anchor. This matters for script extenders: loaders such as
`skse64_loader.exe` can exit almost immediately while `SkyrimSE.exe` remains
active. Post-run INI/plugin synchronization and VFS cleanup are deferred until
the helper reports that all registered USVFS child processes have drained.

Root Builder is prepared before the request is written and cleaned through the
normal post-run/unmount path. A FUSE preview mounted by the Data tab is removed
before an USVFS launch so USVFS receives physical source and destination paths.
Executables living inside a mod are translated to their virtual game path
before the request is serialized.

### Database-backed resolved snapshots

The FUSE catalog is backend-neutral: SQLite persists file fingerprints, while
`VfsCatalog::reconcileAndBuild` produces the resolved in-memory tree. For a
capable USVFS runtime, Fluorine now runs that same reconciliation immediately
before launch and serializes the resolved winners instead of asking Wine to
recursively enumerate every mod directory. Unchanged files are fingerprint
hits; only new or changed files are hashed. Base-game-only files are omitted
because Windows can still reach them directly through the physical Data
directory.

The launcher installs Data roots shallowly, imports the resolved directories
and files with one `usvfsVirtualLinkMappings` call, then reapplies custom write
targets and session file mappings so their priority remains unchanged.
Mappings outside Data continue to use normal recursive installation. Skip
directory and file-suffix rules are applied while the snapshot is built and
again by USVFS.

Capability detection is automatic: Fluorine enables this path only when the
staged x64 DLL exports `usvfsVirtualLinkMappings`. Set
`FLUORINE_USVFS_RESOLVED_SNAPSHOT=0` to force the old recursive path, or `1`
while developing a compatible runtime. A catalog error falls back before the
request is written; an unsupported DLL or failed bulk import falls back inside
the launcher by clearing the partial tree and installing the original mappings
recursively. User cancellation does not launch the game.

The `mapping_install` benchmark record includes `snapshot_entries`,
`snapshot_imported`, and `snapshot_fallback`. The Fluorine log also records
catalog files scanned, fingerprint misses, and files hashed, which makes it
possible to compare database verification plus bulk import against the old
Wine-side traversal.

## Pinned components

The release build downloads and verifies one immutable fork archive:

- [`v0.5.7.2-wine-snapshot.2`](https://github.com/SulfurNitride/usvfs/releases/tag/v0.5.7.2-wine-snapshot.2),
  archive SHA-256
  `e6c38a64a2c6b23cc07411180a8958e026c362e3662f1df6542a72f4adcb6ecf`.
- Release x64 DLL SHA-256
  `2902ec5ac898da59a522b48bc8b6d705758e3b103ef0b7397763688d5a47ceb7`.
- Release x86 DLL SHA-256
  `bafb128bbe05084b929b5fa7ea37dac1448477e1a26d86938994f71d554c1ea7`.
- Full fork x86/x64 Debug/Release build and test matrix
  [`31065013884`](https://github.com/SulfurNitride/usvfs/actions/runs/31065013884).

Omni's patch detects Wine and reports the optional DOS 8.3 short-name field as
absent instead of asking Wine to synthesize it repeatedly. Native Windows keeps
the existing behavior. The release adds the Wine short-name change (`644eebf`)
and resolved snapshot bulk API (`f5dea41`, with release-context compatibility
in `98bc3ed`) over upstream `v0.5.7.2`. Gameplay and performance acceptance
still require a same-save visible A/B.

## Building

Use the repository build entry point:

```bash
./build.sh test
./build.sh
```

The container installs MinGW-w64, compiles the helper as a static x64 Windows
executable, verifies the downloaded runtime hashes, and stages everything under
`usvfs/` beside `ModOrganizer-core`. The USVFS GPLv3 license is staged under
`licenses/`. A developer build without MinGW or `/opt/fluorine-usvfs` still
builds Fluorine and supports FUSE, but cannot launch with the USVFS selection.
The packaged DLL and its dependencies can be checked inside a configured prefix
by running `fluorine-usvfs-launcher.exe --self-test` through the same Proton.

## Portable update identity

The portable launcher publishes its shipped payload into
`~/.local/share/fluorine/bin`. A core-only size/mtime marker is insufficient:
an USVFS DLL, helper, plugin, symlink or executable mode can change while
`ModOrganizer-core` does not. Builds therefore create a typed, deterministic
leaf manifest containing each shipped file digest and mode or symlink target.

Publication validates an immutable private stage, serializes publishers,
preflights ownership conflicts, and writes exact leaves through a restartable
forward transaction. The commit marker is published last. Removed v2-owned
leaves are retired precisely; ambiguous legacy and user-added files are not
recursively deleted. The launcher also opens a shared runtime lease, rechecks
the committed manifest while holding it, and passes that descriptor through
`exec` to the core. The core validates and retains the lease for its complete
lifetime but marks it close-on-exec so games and helpers cannot inherit it.
This preserves one signalable launcher/core PID and prevents an update from
mixing two generations. Direct installed launches hash only the small manifest,
not the full installed tree.

For the first manual update from a legacy top-level-manifest release, close all
running Fluorine Manager windows first because those old processes did not hold
the runtime lease. The in-app updater already waits for the old process before
publication.

## Benchmarking

Change only the VFS selector and keep the modlist, profile, Proton version,
launch options and hardware unchanged. Discard runs that build shaders or other
caches. Run each backend several times in alternating order and record at least:

- click-to-main-menu time;
- cold and warm filesystem-cache state;
- helper log path from the Fluorine log (`<dataPath>/logs/usvfs-*.log`);
- failures, missing files, generated-file placement and shutdown behavior.

Median and spread are more useful than a single best run. Test tools as well as
the game, especially 32-bit child processes, script extenders, behavior engines,
archive tools, local saves, Root Builder and custom overwrite targets.

## Current limitations

- This is experimental and has not yet had broad modlist compatibility testing.
- The pinned snapshot release passed the Windows conformance matrix but still
  needs real-modlist Wine/Proton gameplay and performance testing.
- The resolved snapshot represents directories that contain visible files;
  empty mod directories are not currently materialized from catalog metadata.
- Both bundled architectures are built from the same short-name and bulk
  snapshot source; the normal game path uses x64 while x86 remains required
  for 32-bit children and tools.
- The helper records compact Info-level USVFS diagnostics plus structured
  `[benchmark]` records for Fluorine preparation, request serialization, DLL
  loading, VFS creation, mapping installation, injection, target lifetime,
  child draining and total helper lifetime. Per-file Debug logging is disabled
  because its I/O can perturb startup measurements.
- FUSE-only facilities that depend on Fluorine's in-process runtime index are
  not provided by USVFS itself. Consumers must not assume an index was published
  merely because a virtualized game was launched.

Changes to the USVFS fork should begin with repeatable profiling and tests. In
particular, replacing its recursive global context lock is not a mechanical
mutex swap: nominal read paths mutate state and hook re-entry requires recursive
acquisition. Measure contention, wait time, hold time and recursion before
changing synchronization semantics.

The current combined research branch adds an environment-gated process-local
profiler. `FLUORINE_USVFS_PROFILE=1` enables only atomic accounting and
high-resolution timestamps; normal launches take an immediate disabled branch.
At HookContext teardown it reports total/read/write/recursive/contended lock
acquisitions, cumulative and maximum wait/hold ticks, maximum recursion depth,
per-call-site lock totals, and directory-query API/class/pattern/buffer/result
shapes. Separate timers cover physical parent-directory opens and regular versus
virtual backing queries, including completion status. It logs no paths. The
True North harness exposes this as
`--profile-usvfs` and preserves summaries in the capture directory.

The same fork contains an opt-in generated loose-file application for 100K–1M
file corpora. It validates mapping priority while measuring mapping build,
existing/missing resolution, exact searches and wildcard enumeration,
cold/warm passes and configurable concurrent access. This supplies fast A/B
iteration; real modlist launch captures remain the final behavioral and
performance gate.

## Optimization investigation order

Projected percentages are not evidence. Rank candidate changes by measured
inclusive time, semantic risk and how cleanly an A/B build can isolate them.
The practical order for independent work is:

1. **Skip Wine 8.3 short names — complete.** The pinned x64 DLL contains this
   change and it has already produced repeatable end-to-end improvements.
2. **Remove disabled logging overhead.** This is a small, low-risk hot-path
   experiment and an A/B DLL is straightforward. Expected impact is small.
3. **Hash-based duplicate tracking — already rejected by profiling.** The code
   change and benchmark are easy, but measured duplicate/filter work was only
   about 0.25 seconds; repeating it is not currently useful.
4. **Reduce repeated path conversions/reconstruction.** Instrument conversion
   counts and time first, then remove only demonstrably repeated work. Unicode,
   long-path, separator and case-folding tests are required.
5. **Pack multiple redirected results into one directory-query buffer.** The
   source explicitly identifies this opportunity, but implementing it requires
   correct record alignment and `NextEntryOffset` rewriting for every supported
   information class, buffer size and single-entry/restart mode.
6. **Negative and exact-path caches.** Microbenchmarks are easy; a correct
   implementation is not. Invalidation must cover writes, creates, deletes,
   renames, overwrite changes, mod-priority collisions and external filesystem
   activity. Start with observation-only cache simulation before serving hits.
7. **Precomputed directory listings.** This broadens the same invalidation and
   ordering problem to complete merged directories and needs a comprehensive
   directory-query conformance suite.
8. **Replace or split the exclusive recursive context lock.** Instrument
   contention first. `READ_CONTEXT()` currently reaches mutable per-handle
   search maps, and hook re-entry requires recursion; a mechanical
   `shared_mutex` substitution can race or deadlock.

Before steps 4–8, build a replay/conformance harness for exact-name and wildcard
queries, legacy and Ex APIs, all information classes, small buffers,
single-entry and restart behavior, colliding names, live mutations and nested
hook re-entry. End-to-end launch timings remain the acceptance benchmark, but
the harness is what makes failures reproducible.

## Current True North investigation result

The detailed raw-run ledger is in
[`usvfs-optimization-lab.md`](usvfs-optimization-lab.md). The current practical
disposition is:

| Candidate | Evidence | Disposition |
| --- | --- | --- |
| Omni Wine short-name suppression | Pinned source/release/hash; established reference payload | Keep enabled for x64 Wine/Proton |
| Disabled call logging | Three candidate plus three reference runs; all correctness gates pass; every median delta below 1% | Safe but not a useful load-time optimization |
| Skip exact post-success exhaustion query | Three candidate plus three reference runs; Skyrim median +0.314%, later plugin/menu proxies about 95 ms earlier | Keep isolated for lower-level/human-menu follow-up; not the default |
| Native-wide physical filename | Full four-configuration CI plus Unicode test and one clean game smoke | Correctness/Unicode cleanup; no speed claim from one run |
| Hash duplicate tracker | Full CI and one clean game smoke; prior profile attributes only ~0.25 s to the category | Correctness pass, performance reject |
| Packed results/caches/precomputed listings | Source audit found missing buffer-contract tests and no cross-process/physical invalidation generation | Do not serve results yet; build conformance/observation tooling first |
| Recursive context lock | Deterministic unfixed test proves the first waiter can enter during recursive ownership; fixed build passes full CI and a clean game smoke | Correctness repair ready for review; reader/writer redesign still requires later contention profiling |

All game runs above used 1,144 mappings, initialized both SKSE and Skyrim hooks,
retained the same 24 known DBVO misses, removed the temporary request, performed
post-helper profile sync, produced no new crash and left no process in the exact
Fluorine prefix. On the tested Proton version, USVFS cannot install the
`NtQueryDirectoryFileEx` hook, so the game timings cover the legacy directory
query path; the Windows CI suite supplies direct Ex-path test coverage.
