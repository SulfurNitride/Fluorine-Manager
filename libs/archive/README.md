[![Build status](https://ci.appveyor.com/api/projects/status/hdthueiiuedeb38f?svg=true)](https://ci.appveyor.com/project/Modorganizer2/modorganizer-archive)

# modorganizer-archive

This module provides a wrapper round the 7zip `7z.dll` allowing easy(ish) access to the contents of an archive.

## How to build?

If you want to build this as par of ModOrganizer2, simply use ModOrganizer2 build system.

If you want to build this as a standalone DLL, you can run the following (requires `cmake >= 3.16`):

```batch
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

This will download the two required dependencies (7z sources and [`fmtlib`](https://github.com/fmtlib/fmt)), and
build the DLL under `build/src/Release`.

In order to use the DLL, you need to have the `7z.dll` available in your path, otherwize `CreateArchive()` will
always fail. You can get the `7z.dll` by installing `7z.exe` or by building 7z yourself (building `archive` does
not build `7z`).

## The `Archive` class

The first thing to do is to create an archive wrapper:

```cpp
#include <archive.h>

std::unique_ptr<Archive> CreateArchive();
```

This creates an archive handler.

You can check the [src/archive.h](src/archive.h) header for more details but here are some
of the available methods:

```cpp
/**
 * @brief Check if this Archive wrapper is in a valid state.
 *
 * A non-valid Archive instance usually means that the 7z DLLs could not be loaded properly. Failures
 * to open or extract archives do not invalidate the Archive, so this should only be used to check
 * if the Archive object has been initialized properly.
 *
 * @return true if this instance is valid, false otherwise.
 */
bool Archive::isValid() const;
```

If this returns `false`, this probably means the system cannot find `7z.dll` or it is corrupt, very old (or possibly too new).
**You should check this before calling `open`.**

```cpp
/**
 * @brief Open the given archive.
 *
 * @param archivePath Path to the archive to open.
 * @param passwordCallback Callback to use to ask user for password. This callback must remain
 *   valid until extraction is complete since some types of archives only requires password when
 *   extracting.
 *
 * @return true if the archive was open properly, false otherwise.
 */
bool Archive::open(std::wstring const &archiveName, ArchiveCallbacks::PasswordCallback passwordCallback)
```

This attempts to open the specified archive file. It should manage to open pretty much anything 7zip recognises. It returns `true` on success.
If an error occurs, it returns `false` and `getLastError()` can be used to check the cause of the failure (see the `Archive.h` header for the
list of possible errors):

```cpp
Archive::Error getLastError() const;
```

If `std::wstring passwordChangeCallback()` is not empty, it is called if when password is needed and should return the password to use.

**Note:** this may be called during `extract` rather than during `open`, so should remain usable until the end of the extraction.
If you do not supply this callback, archives with passwords will be unreadable.

Once the archive is opned, you can retrieve the list of files inside using `getFileList()`:

```cpp
const std::vector<FileData*>& getFileList() const;
```

This will return a reference to a vector containing `FileData`. The vector contains non-const pointers so you can actually modify (not assign)
the pointed `FileData` to indicate which files to extract and the path to extract them to.

Once you have updated the `FileData` (see below) you want to extract, you can then perform the extraction using:

```cpp
/**
 * @brief Extract the content of the archive.
 *
 * This function uses the filenames from FileData to obtain the extraction paths of file.
 *
 * @param outputDirectory Path to the directory where the archive should be extracted. If not empty,
 *   conflicting files will be replaced by the extracted ones.
 * @param progressCallback Function called to notify extraction progress. This function is called
 *   when new data is written to the disk, not when the archive is read, so it may take a little
 *   time to start.
 * @param fileChangeCallback Function called when the file currently being extracted changes.
 * @param errorCallback Function called when an error occurs.
 *
 * @return true if the archive was extracted, false otherwise.
 */
bool extract(std::wstring const &outputDirectory, ArchiveCallbacks::ProgressCallback progressCallback,
             ArchiveCallbacks::FileChangeCallback fileChangeCallback, ArchiveCallbacks::ErrorCallback errorCallback)
```

All callbacks are optional, you can pass an empty `std::function` instead (either `nullptr` or `{}`). The purpose of the callbacks:

- `progressCallback(float)` is called during extraction to notify progress.
- `fileChangeCallback(std::wstring const&)` is called when a file starts being extracted.
- `errorCallback(std::wstring const&)` is called if an error occurred, with an appropriate error message. There is not much you can do
    here beyond displaying the message. This will also result in a failure return from `extract`.

Once `extract()` is done, you can call `getFileList()` again and perform a different extractions. `extract()` will clean the list of
`FileData` (unless an error occurred).

You can cancel the extraction at any time by calling:

```cpp
void Archive::cancel();
```

This will cause `extract` to return `false` and `getLastError` to return `ERROR_EXTRACT_CANCELLED`.

Once you are done, do not forget to close the currently opened `Archive`:

```cpp
void Archive::close();
```

## The `FileData` class

As you have seen above, the `getFileList` method returns a reference to a vector of entries about all the files in the archive.
You can see the full declaration of `FileData` in [src/archive.h](src/archive.h). The following methods are the most important
ones:

```cpp
void FileData::addOutputFileName(std::wstring const& filepath)
```

Adds a new output path for this file. The given `filepath` should be relative to the extraction folder specified in `Archive::extract`.
Initially, the list of output paths is empty, so if you do not call `addOutputFileName`, the corresponding file will not be extracted.
You can extract a file in the archive to as many files as you want.

```cpp
std::vector<std::wstring> FileData::getAndClearOutputFileNames()
```

Returns the list of output paths (relative to the extraction folder) for this file and clears it. This is normally only used inside
`extract` but can be used to clear the output filenames after a failure.

Depending on the type of archives, you may have entries corresponding to directories, in which case `FileData::isDirectory()` will
return `true`.
You can "extract" those like normal files, but directories will be automatically created for files if necessary anyway.

## Full example

Below is a full example on how to extract an archive to a given folder:

```cpp
#include <iostream>

#include "archive.h"

int main() {

    // Path to the archive and to the output folder:
    const std::wstring archivePath = L"archive.7z";
    const std::wstring outputFolder = L"output";

    auto archive = CreateArchive();

    if (!archive->isValid()) {
        std::wcerr << "Failed to load the archive module: " << archive->getLastError() << '\n';
        return -1;
    }

    // You can set a log callback if you want:
    archive->setLogCallback([](auto level, auto const& message) {
        std::wcout << message << '\n';
    });

    // Open the archive:
    if (!archive->open(archivePath, nullptr)) {
        std::wcerr << "Failed to open the archive: " << archive->getLastError() << '\n';
        return -1;
    }

    // Get the list of files:
    auto const& files = archive->getFileList();

    // Mark all files for extraction to their path in the archive:
    for (auto *fileData: files) {
        fileData->addOutputFileName(fileData->getFileName());
    }

    // Extract everything (without callbacks):
    auto result = archive->extract(outputFolder, nullptr, nullptr, nullptr);

    if (!result) {
        std::wcerr << "Failed to extract the archive: " << archive->getLastError() << '\n';
        return -1;
    }

    // Close the archive:
    archive->close();

    return 0;
}
```

# Copyright

Copyright (C) 2012 Sebastian Herbord, (C) 2020 MO2 Team. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

See [LICENSE](LICENSE) for more details.
