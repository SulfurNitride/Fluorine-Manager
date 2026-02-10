class IVec3:
    def __init__(self, x: float, y: float, z: float):
        self.x = x
        self.y = y
        self.z = z

    def __str__(self) -> str:
        return f"{self.x}, {self.y}, {self.z}"


class IVec4(IVec3):
    def __init__(self, x: float, y: float, z: float, w: float):
        super().__init__(x, y, z)
        self.w = w

    def __str__(self) -> str:
        return f"{self.x}, {self.y}, {self.z}, f{self.w}"


class IFlag:
    def __init__(self, flag: int):
        self._flag = flag

    def __str__(self) -> str:
        return str(self._flag)

    def assign(self, mask: int):
        self._flag = mask

    def has(self, mask: int) -> bool:
        return bool((self._flag & mask) == mask)

    def set(self, mask: int) -> None:
        self._flag |= mask

    def remove(self, mask: int) -> None:
        self._flag &= ~mask
