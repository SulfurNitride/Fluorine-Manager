# ModOrganizer2 VCPKG Registry

Custom registry for [VCPKG](https://github.com/microsoft/vcpkg/).

## How to use?

Add the following to your `vcpkg-configuration.json` - update the baseline and the list
of packages to your needs:

```json
{
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/ModOrganizer2/vcpkg-registry",
      "baseline": "27d8adbfe9e4ce88a875be3a45fadab69869eb60",
      "packages": ["mo2-cmake", "spdlog"]
    }
  ]
}
```

## Included ports

| Port          | Description          |
| ------------- | -------------------- |
|`asmjit` | Fork of the official `asmjit` port to use a (very) old version of `asmjit` that is compatible with [USVFS](https://github.com/ModOrganizer2/usvfs). |
| `libloot` | Port for [libloot](https://github.com/loot/libloot), using the prebuilt release available. |
| `mo2-cmake` | CMake utilities for ModOrganizer2 components, especially plugins. |
| `mo2-dds-header` | Small port to bring the DirectXTex `DDS.h` header. |
| `mo2-libbsarch` | Port for [libbsarch](https://github.com/ModOrganizer2/libbarch), using the prebuilt release available. |
| `spdlog` | For of the official `spdlog` port to remove `fmt` dependency and force the use of `std::format` instead. |

## Updating a dependency

1. Manually modify `vcpkg.json` and `portfile.cmake` for the dependency you want to update
under ports:
    - Update the version (and other stuff if needed) in `vcpkg.json`.
    - Update (at least) the HASH of the file in `portfile.cmake`.

2. Make a commit with the change, see format of previous commit, e.g.,

    [mo2-libloot] Bump to 0.26.2.

3. Run the following:

    ```powershell
    vcpkg --x-builtin-ports-root=./ports `
      --x-builtin-registry-versions-dir=./versions x-add-version `
      --all --verbose --overwrite-version
    ```

4. Make a commit with all changes under `versions` with message

    [registry] Update versions database.

5. Push the change to the repository.
