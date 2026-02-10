# Linux compatibility shim for Python plugins that import winreg.
# These plugins already handle FileNotFoundError fallback paths when registry
# lookups are unavailable.

HKEY_LOCAL_MACHINE = object()
HKEY_CURRENT_USER = object()


def OpenKey(*_args, **_kwargs):
    raise FileNotFoundError("winreg is not available on this platform")


def QueryValueEx(*_args, **_kwargs):
    raise FileNotFoundError("winreg is not available on this platform")
