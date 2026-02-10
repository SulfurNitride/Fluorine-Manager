import configparser
import hashlib
import os
import re
import shutil
import subprocess
import traceback
from functools import cached_property
from pathlib import Path
from typing import Callable
from xml.etree import ElementTree
from xml.etree.ElementTree import Element

from PyQt6.QtCore import (
    qDebug,
    qInfo,
    qWarning,
)

import mobase

from . import bg3_utils


class BG3PakParser:
    def __init__(self, utils: bg3_utils.BG3Utils):
        self._utils = utils

    _mod_cache: dict[Path, bool] = {}
    _types = {
        "Folder": "",
        "MD5": "",
        "Name": "",
        "PublishHandle": "0",
        "UUID": "",
        "Version64": "0",
    }

    @cached_property
    def _divine_command(self):
        return f"{self._utils.tools_dir / 'Divine.exe'} -g bg3 -l info"

    @cached_property
    def _folder_pattern(self):
        return re.compile("Data|Script Extender|bin|Mods")

    def get_metadata_for_files_in_mod(
        self, mod: mobase.IModInterface, force_reparse_metadata: bool
    ):
        return {
            mod.name(): "".join(
                [
                    self._get_metadata_for_file(mod, file, force_reparse_metadata)
                    for file in sorted(
                        list(Path(mod.absolutePath()).rglob("*.pak"))
                        + (
                            [
                                f
                                for f in Path(mod.absolutePath()).glob("*")
                                if f.is_dir()
                            ]
                            if self._utils.autobuild_paks
                            else []
                        )
                    )
                ]
            )
        }

    def _get_metadata_for_file(
        self,
        mod: mobase.IModInterface,
        file: Path,
        force_reparse_metadata: bool,
    ) -> str:
        meta_ini = Path(mod.absolutePath()) / "meta.ini"
        config = configparser.ConfigParser(interpolation=None)
        config.read(meta_ini, encoding="utf-8")
        try:
            if file.name.endswith("pak"):
                meta_file = (
                    self._utils.plugin_data_path
                    / "temp"
                    / "extracted_metadata"
                    / f"{file.name[: int(len(file.name) / 2)]}-{hashlib.md5(str(file).encode(), usedforsecurity=False).hexdigest()[:5]}.lsx"
                )
                try:
                    if (
                        not force_reparse_metadata
                        and config.has_section(file.name)
                        and (
                            "override" in config[file.name].keys()
                            or "Folder" in config[file.name].keys()
                        )
                    ):
                        return get_module_short_desc(config, file)
                    meta_file.parent.mkdir(parents=True, exist_ok=True)
                    meta_file.unlink(missing_ok=True)
                    out_dir = (
                        str(meta_file)[:-4] if self._utils.extract_full_package else ""
                    )
                    can_continue = True
                    if self.run_divine(
                        f'{"extract-package" if self._utils.extract_full_package else "extract-single-file -f meta.lsx"} -d "{meta_file if not self._utils.extract_full_package else out_dir}"',
                        file,
                    ).returncode:
                        can_continue = False
                    if can_continue and self._utils.extract_full_package:
                        qDebug(f"archive {file} extracted to {out_dir}")
                        if self.run_divine(
                            f'convert-resources -d "{out_dir}" -i lsf -o lsx -x "*.lsf"',
                            out_dir,
                        ).returncode:
                            qDebug(
                                f"failed to convert lsf files in {out_dir} to readable lsx"
                            )
                        extracted_meta_files = list(Path(out_dir).rglob("meta.lsx"))
                        if len(extracted_meta_files) == 0:
                            qInfo(
                                f"No meta.lsx files found in {file.name}, {file.name} determined to be an override mod"
                            )
                            can_continue = False
                        else:
                            shutil.copyfile(
                                extracted_meta_files[0],
                                meta_file,
                            )
                    elif can_continue and not meta_file.exists():
                        qInfo(
                            f"No meta.lsx files found in {file.name}, {file.name} determined to be an override mod"
                        )
                        can_continue = False
                    return self.metadata_to_ini(
                        config, file, mod, meta_ini, can_continue, lambda: meta_file
                    )
                finally:
                    if self._utils.remove_extracted_metadata:
                        meta_file.unlink(missing_ok=True)
                        if self._utils.extract_full_package:
                            Path(str(meta_file)[:-4]).unlink(missing_ok=True)
            elif file.is_dir():
                if self._folder_pattern.search(file.name):
                    return ""
                for folder in bg3_utils.loose_file_folders:
                    if next(file.glob(f"{folder}/*"), False):
                        break
                else:
                    return ""
                qInfo(f"packable dir: {file}")
                if (file.parent / f"{file.name}.pak").exists() or (
                    file.parent / "Mods" / f"{file.name}.pak"
                ).exists():
                    qInfo(
                        f"pak with same name as packable dir exists in mod directory. not packing dir {file}"
                    )
                    return ""
                parent_mod_name = file.parent.name.replace(" ", "_")
                pak_path = (
                    self._utils.overwrite_path
                    / f"Mods/{parent_mod_name}_{file.name}.pak"
                )
                build_pak = True
                if pak_path.exists():
                    try:
                        pak_creation_time = os.path.getmtime(pak_path)
                        for root, _, files in file.walk():
                            for f in files:
                                file_path = root.joinpath(f)
                                try:
                                    if os.path.getmtime(file_path) > pak_creation_time:
                                        break
                                except OSError as e:
                                    qDebug(f"Error accessing file {file_path}: {e}")
                                    break
                        else:
                            build_pak = False
                    except OSError as e:
                        qDebug(f"Error accessing file {pak_path}: {e}")
                        build_pak = False
                if build_pak:
                    pak_path.unlink(missing_ok=True)
                    if self.run_divine(
                        f'create-package -d "{pak_path}"', file
                    ).returncode:
                        return ""
                meta_files = list(file.glob("Mods/*/meta.lsx"))
                return self.metadata_to_ini(
                    config,
                    file,
                    mod,
                    meta_ini,
                    len(meta_files) > 0,
                    lambda: meta_files[0],
                )
            else:
                return ""
        except Exception:
            qWarning(traceback.format_exc())
            return ""

    def run_divine(
        self, action: str, source: Path | str
    ) -> subprocess.CompletedProcess[str]:
        command = f'{self._divine_command} -a {action} -s "{source}"'
        result = subprocess.run(
            command,
            creationflags=subprocess.CREATE_NO_WINDOW,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if result.returncode:
            qWarning(
                f"{command.replace(str(Path.home()), '~', 1).replace(str(Path.home()), '$HOME')}"
                f" returned stdout: {result.stdout}, stderr: {result.stderr}, code {result.returncode}"
            )
        return result

    def get_attr_value(self, root: Element, attr_id: str) -> str:
        default_val = self._types.get(attr_id) or ""
        attr = root.find(f".//attribute[@id='{attr_id}']")
        return default_val if attr is None else attr.get("value", default_val)

    def metadata_to_ini(
        self,
        config: configparser.ConfigParser,
        file: Path,
        mod: mobase.IModInterface,
        meta_ini: Path,
        condition: bool,
        to_parse: Callable[[], Path],
    ):
        config[file.name] = {}
        if condition:
            root = (
                ElementTree.parse(to_parse())
                .getroot()
                .find(".//node[@id='ModuleInfo']")
            )
            if root is None:
                qInfo(f"No ModuleInfo node found in meta.lsx for {mod.name()} ")
            else:
                section = config[file.name]
                folder_name = self.get_attr_value(root, "Folder")
                if file.is_dir():
                    self._mod_cache[file] = (
                        len(list(file.glob(f"*/{folder_name}/**"))) > 1
                        or len(
                            list(file.glob("Public/Engine/Timeline/MaterialGroups/*"))
                        )
                        > 0
                    )
                elif file not in self._mod_cache:
                    # a mod which has a meta.lsx and is not an override mod meets at least one of three conditions:
                    # 1. it has files in Public/Engine/Timeline/MaterialGroups, or
                    # 2. it has files in Mods/<folder_name>/ other than the meta.lsx file, or
                    # 3. it has files in Public/<folder_name>
                    result = self.run_divine(
                        f'list-package --use-regex -x "(/{re.escape(folder_name)}/(?!meta\\.lsx))|(Public/Engine/Timeline/MaterialGroups)"',
                        file,
                    )
                    self._mod_cache[file] = (
                        result.returncode == 0 and result.stdout.strip() != ""
                    )
                if self._mod_cache[file]:
                    for key in self._types:
                        section[key] = self.get_attr_value(root, key)
                else:
                    qInfo(f"pak {file.name} determined to be an override mod")
                    section["override"] = "True"
                    section["Folder"] = folder_name
        else:
            config[file.name]["override"] = "True"
        with open(meta_ini, "w+", encoding="utf-8") as f:
            config.write(f)
        return get_module_short_desc(config, file)


def get_module_short_desc(config: configparser.ConfigParser, file: Path) -> str:
    if not config.has_section(file.name):
        return ""
    section: configparser.SectionProxy = config[file.name]
    return (
        ""
        if "override" in section.keys() or "Name" not in section.keys()
        else bg3_utils.get_node_string(
            folder=section["Folder"],
            md5=section["MD5"],
            name=section["Name"],
            publish_handle=section["PublishHandle"],
            uuid=section["UUID"],
            version64=section["Version64"],
        )
    )
