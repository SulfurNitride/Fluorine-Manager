# External installer references

These files are frozen research inputs used to compare Fluorine's curated-guide
format with other installers. They are not bundled into Fluorine and are not an
authoritative upstream release.

## UMO Graphics Overhaul

- File: `umo-graphics-overhaul-2026-09-02.json`
- Source: <https://modding-openmw.com/lists/graphics-overhaul/json>
- Retrieved: 2026-09-02 (America/Chicago)
- Exact downloaded size: 503,953 bytes
- SHA-256: `b62c29e8e22b14ec02deecf3576bb5b20405ade70da13ebe50d2a4d8496f22c3`
- Entries: 375
- Download records: 497

The source endpoint is generated from Modding-OpenMW's live database. This
dated copy freezes the response for structural comparison. It represents the
MOMW list data that UMO initially consumes, not UMO's fully synchronized local
state. UMO resolves current download metadata during `umo cache sync` and saves
that additional state in its local SQLite `cache.db`.
