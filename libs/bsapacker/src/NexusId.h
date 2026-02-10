#ifndef NEXUSID_H
#define NEXUSID_H

namespace BsaPacker
{
	enum NexusId
	{
		// Skyrim VR, Fallout 4 VR, and TTW don't have Nexus pages so are 0
		Morrowind = 100,
		Oblivion = 101,
		Fallout3 = 120,
		NewVegas = 130,
		Skyrim = 110,
		SkyrimSE = 1704,
		Fallout4 = 1151,
		Enderal = 2736,
		EnderalSE = 3685,
		Starfield = 4187
	};
} // namespace BsaPacker

#endif // NEXUSID_H
