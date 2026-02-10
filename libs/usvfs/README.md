# USVFS

[![License](https://img.shields.io/:license-gpl-blue.svg)](http://www.gnu.org/licenses/gpl-3.0.en.html)
[![Build](https://github.com/ModOrganizer2/usvfs/actions/workflows/build.yml/badge.svg)](https://github.com/ModOrganizer2/usvfs/actions)

USVFS (short for User Space Virtual File System) aims to allow windows applications to
create file or directory links that are visible to only a select set of processes.
It does so by using api hooking to fool file access functions into discovering/opening
files that are in fact somewhere else.

## Current state

USVFS is work in progress and should be considered in alpha state.
It is a core component of Mod Organizer v2 <https://github.com/ModOrganizer2/modorganizer>
and thus receives serious real world testing.

## Building

You will need `cmake`, Python 3+ and `vcpkg` to build USVFS:

```pwsh
cmake --preset vs2022-windows-x64
cmake --build --preset vs2022-windows-x64 --config Release

# only if you need to hook x86 applications
cmake --preset vs2022-windows-x86
cmake --build --preset vs2022-windows-x86 --config Release
```

## Comparison to symbolic links

The following is based on the final goal for USVFS and doesn't necessary reflect the
current development state.

Unlike symbolic file links provided by NTFS

- links aren't visible to all applications but only to those the caller chooses
- links disappear when the "session ends"
- doesn't require write access to the link destination
- doesn't require administrator rights (neither for installation nor for use)
- links are filesystem independent so you can create links on fat32 drives, read-only media and network drives
- can link multiple directories on top of a single destination (overlaying)
- can also "virtually" unlink files, thus make them invisible to processes or replace existing files

There are of course drawbacks

- will always impose a memory and cpu overhead though hopefully those will be marginal
- becomes active only during the initialization phase of each process so it may not be active at the time dependent dlls are loaded
- introduces a new source of bugs that can cause hard to diagnose problems in affected processes
- may rub antivirus software the wrong way as the used techniques are similar to what some malware does.

## License

USVFS is currently licensed under the GPLv3 but this may change in the future.

## Contributing

Contributions are very welcome but please notice that since I'm still undecided on
licensing I have to ask all contributors to agree to future licensing changes.
