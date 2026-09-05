#!/usr/bin/env python3
"""Compile a pinned ModdingLinked checkout into a reviewable Fluorine recipe draft.

This intentionally never downloads the public website. Nexus file labels remain
selectors until a maintainer resolves and pins file IDs/hashes, or until the
authenticated runtime resolver finds exactly one matching file.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable
from urllib.parse import parse_qs, urlparse

from bs4 import BeautifulSoup, Tag


NEXUS_RE = re.compile(r"nexusmods\.com/([^/]+)/mods/(\d+)", re.I)

VNV_EXTENDED_PAGES = (
    "setup.html", "mo2.html", "utilities.html", "bugfix.html", "basefinish.html",
    "hud.html", "gameplay.html", "content.html", "visuals.html", "finish.html",
)


@dataclass(frozen=True)
class GuideSpec:
    recipe_id: str
    display_name: str
    guide_url: str
    repository: str
    game_plugin: str
    pages: tuple[str, ...]
    stores: tuple[str, ...]
    required_games: tuple[str, ...]
    artwork_path: str
    estimated_download_size: int
    estimated_install_size: int
    size_estimate_note: str


SPECS = {
    "vnv-base": GuideSpec(
        "vnv-base", "Viva New Vegas - Base", "https://vivanewvegas.moddinglinked.com/",
        "https://github.com/ModdingLinked/Viva-New-Vegas", "New Vegas",
        ("setup.html", "mo2.html", "utilities.html", "bugfix.html", "basefinish.html"),
        ("Steam", "GOG", "Epic Games"), ("falloutnv",),
        "img/Others/Modlist Card.webp", 2 * 1024**3, 17 * 1024**3,
        "Conservative guide requirement including the isolated game copy.",
    ),
    "vnv-extended": GuideSpec(
        "vnv-extended", "Viva New Vegas - Extended",
        "https://vivanewvegas.moddinglinked.com/",
        "https://github.com/ModdingLinked/Viva-New-Vegas", "New Vegas",
        VNV_EXTENDED_PAGES, ("Steam", "GOG", "Epic Games"), ("falloutnv",),
        "img/Others/Modlist Card.webp", 2 * 1024**3, 17 * 1024**3,
        "Conservative guide requirement including the isolated game copy.",
    ),
    "tbot-essentials": GuideSpec(
        "tbot-essentials", "The Best of Times - Essentials",
        "https://thebestoftimes.moddinglinked.com/",
        "https://github.com/ModdingLinked/The-Best-of-Times", "TTW",
        ("setup.html", "mo2.html", "ttw.html", "essentials.html", "finish.html"),
        ("Steam", "GOG", "Epic Games"),
        ("falloutnv", "fallout3"),
        "img/Others/Card.webp", 2 * 1024**3, 40 * 1024**3,
        "Conservative guide requirement including both isolated game copies and TTW output.",
    ),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def slug(value: str) -> str:
    result = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    return result[:72] or "item"


def git_commit(source: Path) -> str:
    return subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()


def git_commit_date(source: Path) -> str:
    return subprocess.check_output(
        ["git", "-C", str(source), "show", "-s", "--format=%cI", "HEAD"], text=True
    ).strip()


def artwork_url(spec: GuideSpec, commit: str) -> str:
    repository = spec.repository.removesuffix(".git")
    if repository.startswith("https://github.com/"):
        project = repository.removeprefix("https://github.com/")
        path = spec.artwork_path.replace(" ", "%20")
        return f"https://raw.githubusercontent.com/{project}/{commit}/{path}"
    raise SystemExit(f"unsupported artwork repository: {spec.repository}")


def selected_files(card: Tag) -> list[str]:
    files: list[str] = []
    for item in card.find_all("li"):
        marker = item.find("b")
        if not marker or marker.get_text(" ", strip=True).lower() not in {
            "main files", "optional files", "updates", "miscellaneous files"
        }:
            continue
        text = item.get_text(" ", strip=True)
        text = re.sub(r"^(Main Files|Optional Files|Updates|Miscellaneous Files)\s*[-–]\s*",
                      "", text, flags=re.I).strip()
        if text:
            files.append(text)
    if files:
        return files
    # A few guide choices (notably the store-specific 4GB patchers) are
    # presented in expanders instead of cards and use a bare <b> marker.
    for marker in card.find_all("b"):
        category = marker.get_text(" ", strip=True).lower()
        if category not in {"main files", "optional files", "updates",
                            "miscellaneous files"}:
            continue
        tail: list[str] = []
        sibling = marker.next_sibling
        while sibling is not None and not isinstance(sibling, Tag):
            tail.append(str(sibling))
            sibling = sibling.next_sibling
        text = re.sub(r"^\s*[-–]\s*", "", "".join(tail)).strip()
        if text:
            files.append(text)
    return files


def separator_name(card: Tag) -> str:
    for item in card.find_all("li"):
        if "name the separator" not in item.get_text(" ", strip=True).lower():
            continue
        strong = item.find("strong")
        if strong:
            return strong.get_text(" ", strip=True)
    return ""


def associated_download_link(marker: Tag, card: Tag) -> Tag | None:
    """Use a nested list's own link, otherwise the card's primary download."""
    item = marker.find_parent("li")
    listing = item.parent if item else None
    if isinstance(listing, Tag):
        sibling = listing.previous_sibling
        while sibling is not None and not isinstance(sibling, Tag):
            sibling = sibling.previous_sibling
        if isinstance(sibling, Tag):
            nested = sibling.find("a", href=True)
            if nested and NEXUS_RE.search(nested.get("href", "")):
                return nested
    primary_links = card.select("h3.link-download a[href]")
    if len(primary_links) == 1:
        return primary_links[0]
    if len(primary_links) > 1:
        for candidate in marker.find_all_previous("a", href=True):
            if (candidate.find_parent("div", class_="card") is card
                    and candidate in primary_links):
                return candidate
    # Store-specific patchers use bare markers after separate headings.
    for candidate in marker.find_all_previous("a", href=True):
        if candidate.find_parent("div", class_="card") is card:
            return candidate
    return None


def nexus_file_id(url: str) -> int:
    values = parse_qs(urlparse(url).query).get("file_id", [])
    return int(values[0]) if values and values[0].isdigit() else 0


def direct_vnv_artifact(source: Path, page_name: str, link: Tag) -> dict | None:
    if not (source / "files/Vanilla UI Plus New Vegas 9.48.7z").is_file():
        return None
    href = link.get("href", "")
    if href.endswith("Vanilla UI Plus New Vegas 9.48.7z"):
        archive = source / "files/Vanilla UI Plus New Vegas 9.48.7z"
        return {
            "id": "vnv-vanilla-ui-plus", "name": "Vanilla UI Plus",
            "source": "direct", "filename": archive.name,
            "url": "https://vivanewvegas.moddinglinked.com/files/Vanilla%20UI%20Plus%20New%20Vegas%209.48.7z",
            "sourceUrl": "https://vivanewvegas.moddinglinked.com/hud.html#VUI",
            "sha256": sha256(archive), "size": archive.stat().st_size,
        }
    if "Stewie-Tweaks-INIs/releases/latest" not in href:
        return None
    extended = page_name == "finish.html"
    suffix = "Extended_" if extended else ""
    display_suffix = " Extended" if extended else ""
    return {
        "id": "vnv-extended-stewie-tweaks-ini" if extended else "vnv-stewie-tweaks-ini",
        "name": f"Stewie Tweaks - VNV{display_suffix} INI", "source": "direct",
        "filename": f"Stewie_Tweaks-VNV_{suffix}INI.7z",
        "url": ("https://github.com/ModdingLinked/Stewie-Tweaks-INIs/releases/"
                f"download/20197727503/Stewie_Tweaks-VNV_{suffix}INI.7z"),
        "sourceUrl": href,
        "sha256": ("ba7b1b1fe3561f9df78e9fe2659fe0680c15ba7b2926634901ffa44c40bc9262"
                   if extended else
                   "c0a2ec3ee387051c2ff4cea5896bbad662b5cf3b7e84ba3b3c76e0dd9e29d927"),
        "size": 33551 if extended else 33511,
    }


def parse_pages(source: Path, pages: Iterable[str]) -> tuple[list[dict], list[dict]]:
    artifacts: list[dict] = []
    ordered: list[dict] = []
    seen_ids: dict[str, int] = {}
    for page_name in pages:
        soup = BeautifulSoup((source / page_name).read_text(encoding="utf-8"), "html.parser")
        for card in soup.select("div.card"):
            heading = card.find("h3")
            title = heading.get_text(" ", strip=True) if heading else ""
            if "creating a separator" in title.lower():
                if name := separator_name(card):
                    ordered.append({"kind": "separator", "name": name})
                continue

            markers = [node for node in card.find_all("b")
                       if node.get_text(" ", strip=True).lower() in {
                           "main files", "optional files", "updates",
                           "miscellaneous files"}]
            marker_links = {id(link) for node in markers
                            if (link := associated_download_link(node, card)) is not None}
            events: list[tuple[int, str, Tag, Tag | None]] = []
            positions = {id(node): index for index, node in enumerate(card.descendants)
                         if isinstance(node, Tag)}
            for node in markers:
                events.append((positions[id(node)], "marker", node,
                               associated_download_link(node, card)))
            # Explicit Nexus file links such as Goodies INI are complete file
            # selectors even when the guide omits a Main/Optional Files marker.
            for link in card.find_all("a", href=True):
                if id(link) in marker_links or nexus_file_id(link.get("href", "")) <= 0:
                    continue
                if NEXUS_RE.search(link.get("href", "")):
                    events.append((positions[id(link)], "link", link, link))
            # VNV also has deterministic non-Nexus downloads. Treat them as
            # ordered card events so the generated separator placement matches
            # the guide rather than merely appending them at the end.
            for link in card.find_all("a", href=True):
                if direct_vnv_artifact(source, page_name, link):
                    events.append((positions[id(link)], "direct", link, link))

            for _, event_type, node, link in sorted(events, key=lambda event: event[0]):
                if not link:
                    continue
                if event_type == "direct":
                    direct = direct_vnv_artifact(source, page_name, link)
                    if direct and not any(item["id"] == direct["id"] for item in artifacts):
                        artifacts.append(direct)
                        ordered.append({"kind": "mod", "artifact": direct["id"],
                                        "folder": direct["name"], "modId": 0,
                                        "unwrapSingleDirectory": direct["id"].endswith("stewie-tweaks-ini")})
                    continue
                match = NEXUS_RE.search(link.get("href", ""))
                if not match:
                    continue
                file_category = (node.get_text(" ", strip=True).lower()
                                 if event_type == "marker" else "")
                tail: list[str] = []
                sibling = node.next_sibling
                if event_type == "link":
                    file_label = node.get_text(" ", strip=True)
                else:
                    while sibling is not None and not isinstance(sibling, Tag):
                        tail.append(str(sibling))
                        sibling = sibling.next_sibling
                    file_label = re.sub(r"^\s*[-–]\s*", "", "".join(tail)).strip()
                if not file_label:
                    continue
                domain, mod_id = match.group(1).lower(), int(match.group(2))
                mod_name = link.get_text(" ", strip=True)
                base_id = slug(f"{domain}-{mod_id}-{file_label}")
                seen_ids[base_id] = seen_ids.get(base_id, 0) + 1
                artifact_id = (base_id if seen_ids[base_id] == 1
                               else f"{base_id}-{seen_ids[base_id]}")
                artifact = {
                    "id": artifact_id, "name": mod_name, "source": "nexus",
                    "domain": domain, "modId": mod_id,
                    "fileId": nexus_file_id(link["href"]),
                    "fileLabel": file_label, "fileCategory": file_category,
                    "filename": f"{artifact_id}.archive", "sourceUrl": link["href"],
                }
                artifacts.append(artifact)
                ordered.append({"kind": "mod", "artifact": artifact_id,
                                "folder": file_label, "modId": mod_id})
    return artifacts, ordered


def custom_ini(source: Path, pages: Iterable[str]) -> str:
    for page_name in pages:
        soup = BeautifulSoup((source / page_name).read_text(encoding="utf-8"), "html.parser")
        card = soup.select_one("#CustomINI")
        if card and (text := card.find("textarea")):
            return text.get_text().strip() + "\n"
    return ""


def compile_recipe(spec: GuideSpec, source: Path) -> dict:
    missing = [page for page in spec.pages if not (source / page).is_file()]
    if missing:
        raise SystemExit(f"missing guide pages: {', '.join(missing)}")
    artifacts, ordered = parse_pages(source, spec.pages)
    actions: list[dict] = []
    dependencies: list[str] = []

    if spec.recipe_id == "tbot-essentials":
        artifacts[:0] = [
            {"id": "ttw-mpi", "name": "Tale of Two Wastelands MPI package",
             "source": "manual", "filename": "TTW 3.4.0 Installer.mpi", "version": "3.4.0",
             "sourceUrl": "https://mod.pub/ttw/133/files"},
            {"id": "ttw-linux-installer", "name": "TTW Linux Installer", "source": "nexus",
             "domain": "site", "modId": 1657, "fileId": 0,
             "fileLabel": "TTW Linux Installer", "filename": "ttw-linux-installer.archive",
             "latestCompatible": True, "minimumVersion": "0.2.0",
             "sourceUrl": "https://www.nexusmods.com/site/mods/1657"},
        ]

    for game, option, destination in (
        ("falloutnv", "fnvSource", "stock/Fallout New Vegas"),
        ("fallout3", "fo3Source", "stock/Fallout 3"),
    ):
        if game in spec.required_games:
            action_id = f"copy-{game}"
            actions.append({"id": action_id, "type": "copy_game", "name": f"Create isolated {game} copy",
                            "condition": {"isolated": True},
                            "parameters": {"sourceOption": option, "destination": destination}})
            dependencies.append(action_id)

    if spec.recipe_id == "tbot-essentials":
        for artifact_id in ("ttw-mpi", "ttw-linux-installer"):
            acquire_id = f"acquire-{artifact_id}"
            actions.append({"id": acquire_id, "type": "acquire", "name": f"Acquire {artifact_id}",
                            "artifact": artifact_id, "dependsOn": list(dependencies)})
            dependencies = [acquire_id]
        actions.append({"id": "extract-ttw-linux-installer", "type": "extract",
                        "name": "Extract native TTW installer", "artifact": "ttw-linux-installer",
                        "dependsOn": ["acquire-ttw-linux-installer"]})
        actions.append({"id": "verify-ttw-games", "type": "run_native",
                        "name": "Verify clean Fallout 3 and New Vegas sources",
                        "dependsOn": ["extract-ttw-linux-installer", "acquire-ttw-mpi"],
                        "parameters": {"program": "${action:extract-ttw-linux-installer}/mpi_installer",
                                       "arguments": ["verify", "--fo3", "${option:fo3Source}",
                                                     "--fnv", "${option:fnvSource}"]}})
        actions.append({"id": "install-ttw", "type": "run_native",
                        "name": "Build Tale of Two Wastelands",
                        "dependsOn": ["verify-ttw-games"],
                        "parameters": {"program": "${action:extract-ttw-linux-installer}/mpi_installer",
                                       "arguments": ["install", "--mpi", "${artifact:ttw-mpi}",
                                                     "--fo3", "${option:fo3Source}",
                                                     "--fnv", "${option:fnvSource}", "--dest",
                                                     "${instance}/mods/Tale of Two Wastelands"],
                                       "output": "mods/Tale of Two Wastelands"},
                        "validation": {"requiredFiles": ["TaleOfTwoWastelands.esm", "YUPTTW.esm"]}})
        dependencies = ["install-ttw"]

    modlist_visible: list[str] = []
    fomod_choices = {
        "Iron Sights Aligned": ["Yukichigai's Unofficial Patch"],
        "skinned mesh improvement mod": ["FNV Ultimate Edition"],
        "Vanilla UI Plus": [],
        "Clean Vanilla Hud": [],
        "Goodies": ["Better Brotherhood"],
        "Simple Character Expansions": ["YUP", "Goodies"],
    }
    ini_edits = {
        "UI Improvements": ("mods/UI Improvements/config/UII_Config.ini", {
            "ExpandedAttributeInfo": "0", "ExpandedSkillInfo": "0",
        }),
        "Enhanced Movement INI": ("mods/Enhanced Movement INI/config/EnhancedMovement.ini", {
            "iSprintSpeedBonus": "30", "fAgilitySpeedBonus": "0.25", "iCost_AP": "10",
        }),
        "Goodies INI": ("mods/Goodies INI/config/Goodies.ini", {
            "bOnFireLines": "0", "bOnFireAnim": "0",
        }),
        "DiaMoveNVSE Patched": ("mods/DiaMoveNVSE Patched/NVSE/Plugins/DiaMoveNVSE.ini", {
            "bEnableHeadTurn": "false", "fVanityModeXMult": "30",
            "fVanityModeYMult": "10",
        }),
    }
    for entry in ordered:
        if entry["kind"] == "separator":
            modlist_visible.append(f"+{entry['name']}_separator")
            continue
        artifact_id, folder = entry["artifact"], entry["folder"]
        acquire_id, extract_id, install_id = (f"acquire-{artifact_id}",
                                               f"extract-{artifact_id}",
                                               f"install-{artifact_id}")
        condition = None
        if entry["modId"] == 81281:
            condition = {"store": "Epic Games"}
        elif entry["modId"] == 62552:
            condition = {"store": ["Steam", "GOG"]}
        acquire_action = {"id": acquire_id, "type": "acquire", "name": f"Download {folder}",
                          "artifact": artifact_id, "dependsOn": list(dependencies)}
        extract_action = {"id": extract_id, "type": "extract", "name": f"Extract {folder}",
                          "artifact": artifact_id, "dependsOn": [acquire_id]}
        if condition:
            acquire_action["condition"] = condition
            extract_action["condition"] = condition
        actions.append(acquire_action)
        actions.append(extract_action)
        if entry["modId"] == 98738:
            actions.append({"id": install_id, "type": "assisted_tool",
                            "name": "Run Vanilla BSAs Patcher",
                            "dependsOn": [extract_id],
                            "parameters": {
                                "executable": f"${{action:{extract_id}}}/Vanilla BSAs Patcher.exe",
                                "output": "mods/Patched BSAs",
                                "gamePathSubdir": "Data",
                                "sourceUrl": "https://github.com/Ungeziefi/Vanilla-BSAs-Patcher",
                                "instructions": "Use the guide defaults, select the isolated New Vegas game, and set Custom output path to the instance's mods/Patched BSAs folder. HDD users should disable decompression."
                            }, "validation": {"requiredGlobs": ["*.bsa"]}})
            modlist_visible.append("+Patched BSAs")
        elif entry["modId"] == 92289:
            actions.append({"id": install_id, "type": "assisted_tool",
                            "name": "Run Ultimate Edition ESM Fixes installer",
                            "dependsOn": [extract_id],
                            "parameters": {
                                "executable": f"${{action:{extract_id}}}/Installer.exe",
                                "output": "mods/Fixed ESMs",
                                "instructions": "Select the clean New Vegas Root folder as the game path, set the Mod folder to the exact output shown below, then click Install and wait for completion."
                            }, "validation": {"requiredGlobs": ["*.esm"]}})
            modlist_visible.append("+Fixed ESMs")
        elif folder in fomod_choices:
            choices = fomod_choices[folder]
            actions.append({"id": install_id, "type": "assisted_tool",
                            "name": f"Apply guide FOMOD choice for {folder}",
                            "dependsOn": [extract_id],
                            "parameters": {
                                "browseSourceAction": extract_id,
                                "output": f"mods/{folder}",
                                "fomodSelections": choices,
                                "instructions": ("Apply the guide's reviewed FOMOD choices."
                                                 if choices else
                                                 "Apply the FOMOD's default choices.")
                            }, "validation": {"minimumFiles": 1}})
            modlist_visible.append(f"+{folder}")
        elif entry["modId"] in {62552, 81281}:
            executable_name = "FNVpatch.exe" if entry["modId"] == 62552 else "Patcher.exe"
            actions.append({"id": install_id, "type": "run_proton",
                            "name": f"Run {folder}", "dependsOn": [extract_id],
                            "condition": condition,
                            "parameters": {"executable": f"${{action:{extract_id}}}/{executable_name}",
                                           "stageSourceAction": extract_id,
                                           "workingDirectory": "${option:managedGamePath}",
                                           "output": "${option:managedGamePath}"},
                            "validation": {"requiredFiles": ["FalloutNV.exe", "FalloutNV_backup.exe"]}})
        else:
            action_type = "install_root" if entry["modId"] == 67883 else "install_mod"
            install_action = {"id": install_id, "type": action_type,
                              "name": f"Install {folder}", "dependsOn": [extract_id],
                              "parameters": {"sourceAction": extract_id, "folder": folder}}
            if entry.get("unwrapSingleDirectory"):
                install_action["parameters"]["unwrapSingleDirectory"] = True
            if action_type == "install_root":
                install_action["validation"] = {"requiredFiles": [
                    "nvse_loader.exe", "nvse_1_4.dll", "nvse_steam_loader.dll",
                    "Data/NVSE/nvse_config.ini",
                ]}
            else:
                modlist_visible.append(f"+{folder}")
            actions.append(install_action)
        dependencies = [install_id]

        if folder in ini_edits:
            path, values = ini_edits[folder]
            edit_id = f"configure-{slug(folder)}"
            actions.append({
                "id": edit_id, "type": "edit_ini",
                "name": f"Apply guide settings for {folder}",
                "dependsOn": [install_id],
                "parameters": {"path": path, "values": values},
            })
            dependencies = [edit_id]

        if spec.recipe_id == "vnv-extended" and folder == "Anniversary Anim Pack":
            reinstall_id = "reinstall-iron-sights-aligned-for-anniversary-anim-pack"
            actions.append({
                "id": reinstall_id, "type": "assisted_tool",
                "name": "Reapply Iron Sights Aligned for Anniversary Anim Pack",
                "dependsOn": [install_id],
                "parameters": {
                    "browseSourceAction": "extract-newvegas-81933-iron-sights-aligned",
                    "output": "mods/Iron Sights Aligned",
                    "fomodSelections": ["Anniversary Anim Pack",
                                        "Yukichigai's Unofficial Patch"],
                    "instructions": "Reapply the reviewed compatibility choices after the animation pack.",
                },
                "validation": {"minimumFiles": 1},
            })
            dependencies = [reinstall_id]

    if spec.recipe_id == "vnv-extended":
        extended_ini = "+Stewie Tweaks - VNV Extended INI"
        modlist_visible.remove(extended_ini)
        modlist_visible.insert(modlist_visible.index("+Stewie Tweaks") + 1, extended_ini)

    selected_profile = ("Viva New Vegas - Extended"
                        if spec.recipe_id == "vnv-extended" else "Default")
    actions.append({"id": "write-profile", "type": "write_profile",
                    "name": "Write profile, INIs, separators, and load order",
                    "dependsOn": list(dependencies),
                    "parameters": {"profileName": selected_profile}})
    required_profile_files = ["ModOrganizer.ini"]
    if spec.recipe_id == "vnv-extended":
        for profile_name in ("Viva New Vegas - Base", "Viva New Vegas - Extended"):
            required_profile_files += [f"profiles/{profile_name}/modlist.txt",
                                       f"profiles/{profile_name}/FalloutCustom.ini"]
    else:
        required_profile_files += ["profiles/Default/modlist.txt",
                                   "profiles/Default/FalloutCustom.ini"]
    actions.append({"id": "validate-final", "type": "validate_profile",
                    "name": "Validate and register instance", "dependsOn": ["write-profile"],
                    "parameters": {"registerInstance": True},
                    "validation": {"requiredFiles": required_profile_files}})

    loadorder: list[str]
    if spec.recipe_id.startswith("vnv-"):
        all_order = (source / "files/loadorder.txt").read_text(encoding="utf-8").splitlines()
        loadorder = [line for line in all_order if line and not line.startswith("#")]
        if spec.recipe_id == "vnv-base":
            loadorder = loadorder[:21]
    else:
        loadorder = ["FalloutNV.esm", "DeadMoney.esm", "HonestHearts.esm", "OldWorldBlues.esm",
                     "LonesomeRoad.esm", "GunRunnersArsenal.esm", "Fallout3.esm", "Anchorage.esm",
                     "ThePitt.esm", "BrokenSteel.esm", "PointLookout.esm", "Zeta.esm",
                     "CaravanPack.esm", "ClassicPack.esm", "MercenaryPack.esm", "TribalPack.esm",
                     "TaleOfTwoWastelands.esm", "YUPTTW.esm"]
    profile = {"modlist": ["# This file was automatically generated by Fluorine."]
                          + list(reversed(modlist_visible)),
               "plugins": [f"*{name}" for name in loadorder],
               "loadorder": loadorder, "falloutCustomIni": custom_ini(source, spec.pages)}
    if spec.recipe_id == "vnv-extended":
        _, base_ordered = parse_pages(source, SPECS["vnv-base"].pages)
        base_visible: list[str] = []
        for entry in base_ordered:
            if entry["kind"] == "separator":
                base_visible.append(f"+{entry['name']}_separator")
            elif entry["modId"] == 98738:
                base_visible.append("+Patched BSAs")
            elif entry["modId"] == 92289:
                base_visible.append("+Fixed ESMs")
            elif entry["modId"] != 67883:
                base_visible.append(f"+{entry['folder']}")
        base_names = {line[1:] for line in base_visible}
        base_modlist = [
            ("+" if line[1:] in base_names else "-") + line[1:]
            for line in reversed(modlist_visible)
        ]
        extended_modlist = [
            "-Stewie Tweaks - VNV INI" if line == "+Stewie Tweaks - VNV INI" else line
            for line in reversed(modlist_visible)
        ]
        base_order = loadorder[:21]
        profile["profiles"] = [
            {"name": "Viva New Vegas - Base",
             "modlist": ["# This file was automatically generated by Fluorine."]
                        + base_modlist,
             "plugins": [f"*{name}" for name in base_order],
             "loadorder": base_order,
             "falloutCustomIni": custom_ini(source, SPECS["vnv-base"].pages)},
            {"name": "Viva New Vegas - Extended",
             "modlist": ["# This file was automatically generated by Fluorine."]
                        + extended_modlist,
             "plugins": [f"*{name}" for name in loadorder],
             "loadorder": loadorder,
             "falloutCustomIni": custom_ini(source, spec.pages)},
        ]

    commit = git_commit(source)
    artwork = source / spec.artwork_path
    if not artwork.is_file():
        raise SystemExit(f"missing guide artwork: {spec.artwork_path}")
    return {
        "schemaVersion": 1, "id": spec.recipe_id, "displayName": spec.display_name,
        "version": "0.1.0", "description": "Reviewed automation draft generated from the pinned guide pages.",
        "gamePlugin": spec.game_plugin, "guideUrl": spec.guide_url,
        "sourceRepository": spec.repository, "sourceCommit": commit,
        "metadata": {
            "updatedAt": git_commit_date(source),
            "artwork": {"url": artwork_url(spec, commit), "sha256": sha256(artwork)},
            "sizeEstimates": {
                "downloadBytes": spec.estimated_download_size,
                "installedBytes": spec.estimated_install_size,
                "note": spec.size_estimate_note,
            },
        },
        "pages": [{"path": page, "sha256": sha256(source / page)} for page in spec.pages]
                 + ([{"path": "files/loadorder.txt", "sha256": sha256(source / "files/loadorder.txt")}]
                    if spec.recipe_id.startswith("vnv-") else []),
        "supportedStores": list(spec.stores), "requiredGames": list(spec.required_games),
        "artifacts": artifacts, "actions": actions, "profile": profile,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--guide", choices=sorted(SPECS), required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rendered = json.dumps(compile_recipe(SPECS[args.guide], args.source.resolve()),
                          indent=2, ensure_ascii=False) + "\n"
    if args.check:
        if not args.output or not args.output.is_file() or args.output.read_text() != rendered:
            raise SystemExit("generated recipe differs from the checked-in recipe")
        return
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")


if __name__ == "__main__":
    main()
