# Linux compatibility shim for optional lzokay dependency used by some
# basic_games modules (e.g. STALKER Anomaly save parsing).


def decompress(_data, _size=None):
    raise RuntimeError("lzokay is not installed")
