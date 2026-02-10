from __future__ import annotations

import typing

import mobase

__all__ = ["get_from_callback", "get_value", "get_variants", "set_from_callback"]

def get_from_callback(
    arg0: typing.Callable[[mobase.GuessedString], None],
) -> str: ...
def get_value(arg0: str | mobase.GuessedString) -> str: ...
def get_variants(arg0: str | mobase.GuessedString) -> set[str]: ...
def set_from_callback(
    arg0: mobase.GuessedString, arg1: typing.Callable[[mobase.GuessedString], None]
) -> None: ...
