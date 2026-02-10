# -*- encoding: utf-8 -*-

import io
from enum import IntFlag
from typing import List

from .XRIO import XRReader
from .XRMath import IFlag, IVec3
from .XRNET import XRNETState


class XRFlag(IntFlag):
    CHUNK_ALIFE = 0x0
    CHUNK_SPAWN = 0x1
    CHUNK_OBJECT = 0x2
    CHUNK_GAME_TIME = 0x5
    CHUNK_REGISTRY = 0x9

    SPAWN_VERSION = 1 << 5

    MSG_UPDATE = 0x0
    MSG_SPAWN = 0x1

    TRADER_INFINITE_AMMO = 0x1


class XRAbstract:
    def __init__(self, name: str = ""):
        self._valid = False
        self.name = name
        self.name_replace = ""
        self.rp = 0xFE
        self.position = IVec3(0.0, 0.0, 0.0)
        self.angle = IVec3(0.0, 0.0, 0.0)
        self.respawn_time = 0
        self.id = 0xFFFF
        self.id_parent = 0xFFFF
        self.id_phantom = 0xFFFF
        self.flags = IFlag(0)
        self.version = 0
        self.game_type = IFlag(0)
        self.script_version = 0
        self.client_data: List[int] = []
        self.spawn_id = 0
        self.ini_str = ""

    def read_spawn(self, reader: XRReader):
        spawn = reader.u16()
        if spawn != XRFlag.MSG_SPAWN:
            self._valid = False
            return
        self.name = reader.str()
        self.name2 = reader.str()
        reader.seek(1, io.SEEK_CUR)  # temp_gt
        self.rp = reader.u8()
        self.position = reader.fvec3()
        self.angle = reader.fvec3()
        self.respawn_time = reader.u16()
        self.id = reader.u16()
        self.id_parent = reader.u16()
        self.id_phantom = reader.u16()
        self.flags = IFlag(reader.u16())
        if self.flags.has(XRFlag.SPAWN_VERSION):
            self.version = reader.u16()
        if self.version == 0:
            reader._pos -= 2  # pyright: ignore[reportPrivateUsage]
            return
        if self.version > 120:
            self.game_type.set(reader.u16())
        if self.version > 69:
            self.script_version = reader.u16()
        if self.version > 70:
            cl_size = 0
            if self.version > 93:
                cl_size = reader.u16()
            else:
                cl_size = reader.u8()
            if cl_size > 0:
                for _ in range(cl_size):
                    data = reader.u8()
                    self.client_data.append(data)
        if self.version > 79:
            self.spawn_id = reader.u16()
        self._valid = True

    def __bool__(self):
        return self._valid


class XRVisual:
    def __init__(self):
        self.visual_name = ""
        self.startup_animation = ""
        self.flags = IFlag(0)

    def read_visual(self, reader: XRReader, version: int):
        self.visual_name = reader.str()
        self.flags = IFlag(reader.u8())


class XRBoneData:
    def __init__(self):
        self.bones_mask = -1
        self.root_bone = 0
        self.min = IVec3(0.0, 0.0, 0.0)
        self.max = IVec3(0.0, 0.0, 0.0)
        self.bones: list[XRNETState] = []

    def load(self, reader: XRReader):
        self.bones_mask = reader.u64()
        self.root_bone = reader.u16()
        self.min = reader.fvec3()
        self.max = reader.fvec3()
        bones_count = reader.u16()
        for _ in range(bones_count):
            bone = XRNETState()
            bone.read(reader, self.min, self.max)
            self.bones.append(bone)


class XRSkeleton:
    def __init__(self):
        self.source_id = -1
        self.saved_bones = XRBoneData()

    def read_state(self, reader: XRReader):
        self.visual_animation = reader.str()
        flags = IFlag(reader.u8())
        self.source_id = reader.u16()
        if flags.has(4):
            self.saved_bones.load(reader)


class XRObject(XRAbstract):
    def __init__(self):
        super().__init__()
        self.graph_id = 0
        self.distance = 0.0
        self.direct_control = True
        self.node_id = 0
        self.story_id = -1
        self.spawn_story_id = -1

    def read_spawn(self, reader: XRReader):
        super().read_spawn(reader)
        size = reader.u16()
        if size <= 2:
            self._valid = False
            return
        self.read_state(reader)

    def read_state(self, reader: XRReader):
        self.graph_id = reader.u16()
        self.distance = reader.float()
        self.direct_control = bool(reader.u32())
        self.node_id = reader.u32()
        self.flags.set(reader.u32())
        self.ini_str = reader.str()
        self.story_id = reader.u32()
        self.spawn_story_id = reader.u32()

    def read_update(self, reader: XRReader):
        update = reader.u16()
        if update != XRFlag.MSG_UPDATE:
            self._valid = False
            return


class XRDynamicObject(XRObject):
    def __init__(self):
        super().__init__()
        self.time_id = -1
        self.switch_counter = 0


class XRDynamicObjectVisual(XRDynamicObject, XRVisual):
    def read_state(self, reader: XRReader):
        XRDynamicObject.read_state(self, reader)
        if self.version > 31:
            XRVisual.read_visual(self, reader, self.version)


class XRCreatureAbstract(XRDynamicObjectVisual):
    def __init__(self):
        super().__init__()
        self.team = 0
        self.squad = 0
        self.group = 0
        self.health = 1.0
        self.dynamic_out: list[int] = []
        self.dynamic_in: list[int] = []
        self.killer_id = -1
        self.death_time = 0

    def read_state(self, reader: XRReader):
        super().read_state(reader)
        self.team = reader.u8()
        self.squad = reader.u8()
        self.group = reader.u8()
        self.health = reader.float() * 100

        for _ in range(reader.u32()):
            _id = reader.u16()
            self.dynamic_out.append(_id)

        for _ in range(reader.u32()):
            _id = reader.u16()
            self.dynamic_in.append(_id)

        self.killer_id = reader.u16()
        self.death_time = reader.u64()


class XRTraderAbstract:
    def __init__(self):
        self.money = 0
        self.max_item_mass = 0
        self.character_name = ""
        self.character_name_str = ""
        self.character_profile = ""
        self.specific_character = ""
        self.community_index = -1
        self.rank = -1  # -MAX_INT?
        self.reputation = -1  # -MAX_INT?
        self.dead_body_can_take = True
        self.dead_body_closed = False
        self.trader_flags = IFlag(0)
        self.trader_flags.remove(XRFlag.TRADER_INFINITE_AMMO)

    def read_state(self, reader: XRReader):
        self.money = reader.u32()
        self.specific_character = reader.str()
        self.trader_flags.assign(reader.u32())
        self.character_profile = reader.str()
        self.community_index = reader.s32()
        self.rank = reader.s32()
        self.reputation = reader.s32()
        self.character_name_str = reader.str()
        self.dead_body_can_take = reader.u8() == 1
        self.dead_body_closed = reader.u8() == 1


class XRCreatureActor(XRCreatureAbstract, XRTraderAbstract, XRSkeleton):
    def __init__(self):
        XRCreatureAbstract.__init__(self)
        XRTraderAbstract.__init__(self)
        XRSkeleton.__init__(self)
        self.state = 0
        self.acceleration = IVec3(0.0, 0.0, 0.0)
        self.velocity = IVec3(0.0, 0.0, 0.0)
        self.radiation = 0.0
        self.weapon = 0
        self.num_items = 0
        self.holder_id = -1

    def read_spawn(self, reader: XRReader):
        XRCreatureAbstract.read_spawn(self, reader)

    def read_state(self, reader: XRReader):
        XRCreatureAbstract.read_state(self, reader)
        XRTraderAbstract.read_state(self, reader)
        XRSkeleton.read_state(self, reader)
        self.holder_id = reader.u16()

    def read_update(self, reader: XRReader):
        XRCreatureAbstract.read_update(self, reader)
        # XRTraderAbstract.read_update(self, reader)  # Future?
        self.state = reader.u16()
        # acceleration (r_sdir)
        reader.seek(2, io.SEEK_CUR)  # u16
        reader.seek(4, io.SEEK_CUR)  # float
        # velocity (r_sdir)
        reader.seek(2, io.SEEK_CUR)  # u16
        reader.seek(4, io.SEEK_CUR)  # float
        self.radiation = reader.float()
        self.weapon = reader.u8()
        self.num_items = reader.u16()
