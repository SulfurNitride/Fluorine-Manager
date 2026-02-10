import io
import struct
from datetime import datetime
from pathlib import Path
from typing import BinaryIO, Optional, cast

import lzokay  # pyright: ignore[reportMissingTypeStubs]

from .XRIO import XRReader, XRStream
from .XRObject import XRCreatureActor, XRFlag


class XRSave:
    filepath: Path
    player: XRCreatureActor

    _factions = {
        0: "Loner",
        1: "Monster",
        2: "Trader",
        3: "Army",
        4: "Sin",
        5: "Bandit",
        6: "Duty",
        7: "Ecologist",
        8: "Freedom",
        9: "Mercenary",
        10: "Army",
        11: "Monolith",
        12: "Sin",
        13: "Loner",
        14: "Zombified",
        15: "Clear Sky",
        16: "UNISG",
        17: "Renegade",
        18: "Loner",
        19: "Bandit",
        20: "Duty",
        21: "Freedom",
        22: "Clear Sky",
        23: "Ecologist",
        24: "Mercenary",
        25: "Military",
        26: "Monolith",
        27: "Zombified",
        28: "Sin",
        29: "UNISG",
        30: "Renegade",
        31: "Participant",
    }

    _ranks = {
        1999: "Novice",
        3999: "Trainee",
        6999: "Experienced",
        9999: "Professional",
        14999: "Veteran",
        20999: "Expert",
        27999: "Master",
        "max": "Legend",
    }

    _reputation = {
        -2000: "Terrible",
        -1500: "Really Bad",
        -1000: "Very Bad",
        -500: "Bad",
        499: "Netural",
        999: "Good",
        1499: "Very Good",
        1999: "Really Good",
        "max": "Excellent",
    }

    def __init__(self, filepath: Path):
        self.filepath = filepath
        self.fetchInfo()
        with open(filepath, "rb") as file:
            stream = self.readFile(file)
            if stream:
                self.readObject(stream)

    def fetchInfo(self):
        self.splitInfo()
        self.time = self.filepath.stat().st_mtime
        self.time_date = datetime.fromtimestamp(self.time)
        self.time_fmt = self.time_date.strftime("%I:%M %m/%d/%Y")

    def splitInfo(self):
        save_clean = self.filepath.name.split(".scop", 1)[0]
        save_split = save_clean.split(" - ", 1)
        self.user = save_split[0]
        if len(save_split) > 1:
            save_end = save_split[1].split("_", 1)
            if len(save_end) > 1:
                self.save_fmt = f"{save_end[0]} (#{save_end[1]})".title()
            else:
                self.save_fmt = f"{save_end[0]}".title()
        else:
            self.save_fmt = "Unknown"

    def readFile(self, file: BinaryIO) -> Optional[XRStream]:
        size = self.filepath.stat().st_size
        if size < 8:
            return None

        (start, version, source) = struct.unpack("@iii", file.read(12))
        if (start == -1) and (version >= 6):
            file.seek(12)
            data = file.read(size - 12)
            return XRStream(
                cast(
                    bytes,
                    lzokay.decompress(  # pyright: ignore[reportUnknownMemberType]
                        data, source
                    ),
                )
            )

        return None

    def readObject(self, stream: XRStream):
        chunk = stream.open_chunk(XRFlag.CHUNK_OBJECT)
        if chunk:
            chunk.seek(4, io.SEEK_CUR)  # obj_count
            count_spawn = chunk.u16()
            spawn = XRReader(chunk.read(count_spawn))
            actor = XRCreatureActor()
            actor.read_spawn(spawn)
            count_update = chunk.u16()
            update = XRReader(chunk.read(count_update))
            actor.read_update(update)
            if actor:
                self.player = actor
        return None

    def getFaction(self) -> str:
        player = self.player
        if player:
            player_faction = player.community_index
            for faction in self._factions:
                if player_faction == faction:
                    return self._factions[faction]
        return "Unknown"

    def getRank(self) -> str:
        player = self.player
        if player:
            player_rank = player.rank
            for rank in self._ranks:
                if isinstance(rank, int):
                    if player_rank <= rank:
                        return self._ranks[rank]
        return self._ranks["max"]

    def getReputation(self) -> str:
        player = self.player
        if player:
            player_rep = player.reputation
            for rep in self._reputation:
                if isinstance(rep, int):
                    if player_rep <= rep:
                        return self._reputation[rep]
        return self._reputation["max"]
