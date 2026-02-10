from typing import TypedDict

PLUGIN_NAME = "Oblivion Remastered Support Plugin"


class UE4SSModInfo(TypedDict):
    mod_name: str
    mod_enabled: bool


DEFAULT_UE4SS_MODS: list[UE4SSModInfo] = [
    {"mod_name": "BPML_GenericFunctions", "mod_enabled": True},
    {"mod_name": "BPModLoaderMod", "mod_enabled": True},
]
