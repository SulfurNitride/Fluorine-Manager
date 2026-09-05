# Curated guide manifests

Fluorine installs curated guides from these reviewed, version-controlled files.
It does not scrape a guide website during an installation.

The layout follows UMO's split between mod-list data and pre-synchronized
download data:

- `catalog.json` is the trusted list of available manifests and their hashes.
- `<guide>.json` describes source provenance, artifacts, actions, profiles,
  separators, load order, commit-pinned artwork, update date, and conservative
  disk-space estimates.
- `<guide>.lock.json` contains reviewed Nexus file-ID and archive metadata pins.
- `tools/guide_recipe_compiler/compile.py` reads a pinned local guide checkout
  and proposes a new manifest for review.
- `tools/guide_recipe_compiler/lock.py` resolves every Nexus selector for a
  reviewed manifest. Set `NEXUS_ACCESS_TOKEN` or `NEXUS_API_KEY`; this command
  is never run by the application.
- `tools/guide_recipe_compiler/catalog.py` refreshes catalog hashes after a
  manifest or lock update.

An incomplete lock is supported: Fluorine falls back to its authenticated
Nexus metadata resolver, like UMO falls back to synchronization when
pre-synchronized data is unavailable. Release manifests should normally be
fully locked.
