/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "formatinto.h"
#include "Common/LogicAudio.h"
#include "Common/DataChunk.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/LatchRestore.h"
#include "Common/MessageStream.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Shell.h"
#include "GameClient/View.h"
#include "GameClient/CampaignManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptConditions.h"
#include "GameLogic/ScriptEngine.h"
#include "ScriptEngineInternal.h"
#include "GameLogic/SidesList.h"
#define DEFINE_PARTICLE_SYSTEM_NAMES 1
#include "GameClient/ParticleSys.h"
#include "Common/MapObject.h"
#include "W3DDevice/GameClient/W3DAssetManagerExposed.h"

// Particle editor support: the particle system INI writer

// Writes a particle system as INI text (the map editor saves particle systems with it).
extern void _writeSingleParticleSystem( File *out, ParticleSystemTemplate *particleTemplate );
#define ARBITRARY_BUFF_SIZE	128
#define FORMAT_STRING "{:.2f}"
#define FORMAT_STRING_LEADING_STRING "{}{:.2f}"

// all ThingTemplates can be thrown with a particle system, so...


constexpr char BACKUP_FILE_NAME[] = "Data\\INI\\ParticleSystem";
constexpr char BACKUP_EXT[] = "BAK";


static const std::string HEADER =					"ParticleSystem";
static const std::string SEP_SPACE =			" ";
static const std::string SEP_HEAD	=				"  ";
static const std::string SEP_EOL =				"\n";
static const std::string SEP_TAB =				"\t";
static const std::string STR_TRUE	=				"Yes";
static const std::string STR_FALSE =			"No";
static const std::string EQ_WITH_SPACES	=	" = ";
static const std::string STR_R = 					"R:";
static const std::string STR_G =					"G:";
static const std::string STR_B =					"B:";
static const std::string STR_X = 					"X:";
static const std::string STR_Y =					"Y:";
static const std::string STR_Z =					"Z:";

static const std::string STR_END =				"End";

static const std::string F_PRIORITY =			"Priority";

static const std::string F_ISONESHOT =		"IsOneShot";
static const std::string F_SHADER =				"Shader";
static const std::string F_TYPE =					"Type";
static const std::string F_PARTICLENAME =	"ParticleName";
static const std::string F_ANGLEX =				"AngleX";
static const std::string F_ANGLEY =				"AngleY";
static const std::string F_ANGLEZ	=				"AngleZ";
static const std::string F_ANGLERATEX	=		"AngularRateX";
static const std::string F_ANGLERATEY	=		"AngularRateY";
static const std::string F_ANGLERATEZ	=		"AngularRateZ";
static const std::string F_ANGLEDAMP =		"AngularDamping";
static const std::string F_VELOCITYDAMP	=	"VelocityDamping";
static const std::string F_GRAVITY =			"Gravity";
static const std::string F_SLAVESYSTEM =	"SlaveSystem";
static const std::string F_SLAVEPOS =			"SlavePosOffset";
static const std::string F_ATTACHED =			"PerParticleAttachedSystem";
static const std::string F_LIFETIME =			"Lifetime";
static const std::string F_SYSLIFETIME =	"SystemLifetime";
static const std::string F_SIZE =					"Size";
static const std::string F_STARTSIZERATE ="StartSizeRate";
static const std::string F_SIZERATE =			"SizeRate";
static const std::string F_SIZERATEDAMP =	"SizeRateDamping";

static const std::string F_ALPHA1 =				"Alpha1";
static const std::string F_ALPHA2 =				"Alpha2";
static const std::string F_ALPHA3 =				"Alpha3";
static const std::string F_ALPHA4 =				"Alpha4";
static const std::string F_ALPHA5 =				"Alpha5";
static const std::string F_ALPHA6 =				"Alpha6";
static const std::string F_ALPHA7 =				"Alpha7";
static const std::string F_ALPHA8 =				"Alpha8";

static const std::string F_COLOR1 =				"Color1";
static const std::string F_COLOR2 =				"Color2";
static const std::string F_COLOR3 =				"Color3";
static const std::string F_COLOR4 =				"Color4";
static const std::string F_COLOR5 =				"Color5";
static const std::string F_COLOR6 =				"Color6";
static const std::string F_COLOR7 =				"Color7";
static const std::string F_COLOR8 =				"Color8";
static const std::string F_COLORSCALE =		"ColorScale";

static const std::string F_BURSTDELAY =		"BurstDelay";
static const std::string F_BURSTCOUNT =		"BurstCount";
static const std::string F_INITIALDELAY =	"InitialDelay";
static const std::string F_DRIFTVELOCITY ="DriftVelocity";

static const std::string F_VELOCITYTYPE =	"VelocityType";

static const std::string F_VELORTHOX =		"VelOrthoX";
static const std::string F_VELORTHOY =		"VelOrthoY";
static const std::string F_VELORTHOZ =		"VelOrthoZ";

static const std::string F_VELSPHERE =		"VelSpherical";
static const std::string F_HEMISPHERE	= 	"VelHemispherical";

static const std::string F_VELCYLRAD =		"VelCylindricalRadial";
static const std::string F_VELCYLNOR =		"VelCylindricalNormal";

static const std::string F_VELOUTWARD =		"VelOutward";
static const std::string F_VELOUTOTHER =	"VelOutwardOther";

static const std::string F_VOLUMETYPE = 	"VolumeType";

static const std::string F_VOLLINESTART =	"VolLineStart";
static const std::string F_VOLLINEEND =		"VolLineEnd";

static const std::string F_VOLBOXHALF	=		"VolBoxHalfSize";
static const std::string F_VOLSPHERERAD	=	"VolSphereRadius";
static const std::string F_VOLCYLRAD =		"VolCylinderRadius";
static const std::string F_VOLCYLLEN =		"VolCylinderLength";
static const std::string F_ISHOLLOW =			"IsHollow";
static const std::string F_ISXYPLANAR =		"IsGroundAligned";
static const std::string F_ISEMITABOVEGROUNDONLY 
																			=		"IsEmitAboveGroundOnly";
static const std::string F_ISPARTICLEUPTOWARDSEMITTER 
																			=		"IsParticleUpTowardsEmitter";

static const std::string F_WINDMOTION = "WindMotion";
static const std::string F_WINDANGLECHANGEMIN = "WindAngleChangeMin";
static const std::string F_WINDANGLECHANGEMAX = "WindAngleChangeMax";
static const std::string F_WINDPINGPONGSTARTANGLEMIN = "WindPingPongStartAngleMin";
static const std::string F_WINDPINGPONGSTARTANGLEMAX = "WindPingPongStartAngleMax";
static const std::string F_WINDPINGPONGENDANGLEMIN = "WindPingPongEndAngleMin";
static const std::string F_WINDPINGPONGENDANGLEMAX = "WindPingPongEndAngleMax";

void _writeSingleParticleSystem( File *out, ParticleSystemTemplate *templ )
{
	if (!templ || !out || templ->getName().isEmpty()) {
		// sanity
		return;
	}

	static char buff1[ARBITRARY_BUFF_SIZE];
	static char buff2[ARBITRARY_BUFF_SIZE];
	static char buff3[ARBITRARY_BUFF_SIZE];
	static char buff4[ARBITRARY_BUFF_SIZE];
	

	// the .append looks REALLY ugly, but this code was written with streams in mind, and so 
	// these were all originally << (feed-operator for streams)
	// I might come back and re-write this later, if there are enough complaints. ;-) jkmcd
	// in the meantime, move along...
	std::string thisEntry = "";
	thisEntry.append(HEADER).append(SEP_SPACE).append(templ->getName().str()).append(SEP_EOL);
	thisEntry.append(SEP_HEAD).append(F_PRIORITY).append(EQ_WITH_SPACES).append(ParticlePriorityNames[templ->m_priority]).append(SEP_EOL);
	thisEntry.append(SEP_HEAD).append(F_ISONESHOT).append(EQ_WITH_SPACES).append((templ->m_isOneShot ? STR_TRUE : STR_FALSE)).append(SEP_EOL);
	thisEntry.append(SEP_HEAD).append(F_SHADER).append(EQ_WITH_SPACES).append(ParticleShaderTypeNames[templ->m_shaderType]).append(SEP_EOL);
	thisEntry.append(SEP_HEAD).append(F_TYPE).append(EQ_WITH_SPACES).append(ParticleTypeNames[templ->m_particleType]).append(SEP_EOL);
	thisEntry.append(SEP_HEAD).append(F_PARTICLENAME).append(EQ_WITH_SPACES).append(templ->m_particleTypeName.str()).append(SEP_EOL);
	
	// Write AngleX/Y and AngularRateX/Y (Generals data) only if set; the Zero Hour data never sets
	// them, and 0 is the default when reading.
	if (templ->m_angleX.getMinimumValue() != 0.0f || templ->m_angleX.getMaximumValue() != 0.0f) {
		WWFormat::Into(buff1, FORMAT_STRING, templ->m_angleX.getMinimumValue());
		WWFormat::Into(buff2, FORMAT_STRING, templ->m_angleX.getMaximumValue());
		thisEntry.append(SEP_HEAD).append(F_ANGLEX).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	}
	if (templ->m_angleY.getMinimumValue() != 0.0f || templ->m_angleY.getMaximumValue() != 0.0f) {
		WWFormat::Into(buff1, FORMAT_STRING, templ->m_angleY.getMinimumValue());
		WWFormat::Into(buff2, FORMAT_STRING, templ->m_angleY.getMaximumValue());
		thisEntry.append(SEP_HEAD).append(F_ANGLEY).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	}

	WWFormat::Into(buff1, FORMAT_STRING, templ->m_angleZ.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_angleZ.getMaximumValue());
	thisEntry.append(SEP_HEAD).append(F_ANGLEZ).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);

	if (templ->m_angularRateX.getMinimumValue() != 0.0f || templ->m_angularRateX.getMaximumValue() != 0.0f) {
		WWFormat::Into(buff1, FORMAT_STRING, templ->m_angularRateX.getMinimumValue());
		WWFormat::Into(buff2, FORMAT_STRING, templ->m_angularRateX.getMaximumValue());
		thisEntry.append(SEP_HEAD).append(F_ANGLERATEX).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	}
	if (templ->m_angularRateY.getMinimumValue() != 0.0f || templ->m_angularRateY.getMaximumValue() != 0.0f) {
		WWFormat::Into(buff1, FORMAT_STRING, templ->m_angularRateY.getMinimumValue());
		WWFormat::Into(buff2, FORMAT_STRING, templ->m_angularRateY.getMaximumValue());
		thisEntry.append(SEP_HEAD).append(F_ANGLERATEY).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	}

	WWFormat::Into(buff1, FORMAT_STRING, templ->m_angularRateZ.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_angularRateZ.getMaximumValue());
	thisEntry.append(SEP_HEAD).append(F_ANGLERATEZ).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);

	WWFormat::Into(buff1, FORMAT_STRING, templ->m_angularDamping.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_angularDamping.getMaximumValue());
	thisEntry.append(SEP_HEAD).append(F_ANGLEDAMP).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);

	WWFormat::Into(buff1, FORMAT_STRING, templ->m_velDamping.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_velDamping.getMaximumValue());
	thisEntry.append(SEP_HEAD).append(F_VELOCITYDAMP).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING, templ->m_gravity);
	thisEntry.append(SEP_HEAD).append(F_GRAVITY).append(EQ_WITH_SPACES).append(buff1).append(SEP_EOL);
	if (!templ->m_slaveSystemName.isEmpty()) {
		thisEntry.append(SEP_HEAD).append(F_SLAVESYSTEM).append(EQ_WITH_SPACES).append(templ->m_slaveSystemName.str()).append(SEP_EOL);
		WWFormat::Into(buff1, FORMAT_STRING_LEADING_STRING, STR_X.c_str(), templ->m_slavePosOffset.x);
		WWFormat::Into(buff2, FORMAT_STRING_LEADING_STRING, STR_Y.c_str(), templ->m_slavePosOffset.y);
		WWFormat::Into(buff3, FORMAT_STRING_LEADING_STRING, STR_Z.c_str(), templ->m_slavePosOffset.z);
		thisEntry.append(SEP_HEAD).append(F_SLAVEPOS).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_EOL);
	}

	if (!templ->m_attachedSystemName.isEmpty()) {
		thisEntry.append(SEP_HEAD).append(F_ATTACHED).append(EQ_WITH_SPACES).append(templ->m_attachedSystemName.str()).append(SEP_EOL);
	}

	WWFormat::Into(buff1, FORMAT_STRING, templ->m_lifetime.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_lifetime.getMaximumValue());
	thisEntry.append(SEP_HEAD).append(F_LIFETIME).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);

	WWFormat::Into(buff1, "{:d}", templ->m_systemLifetime);
	thisEntry.append(SEP_HEAD).append(F_SYSLIFETIME).append(EQ_WITH_SPACES).append(buff1).append(SEP_EOL);

	WWFormat::Into(buff1, FORMAT_STRING, templ->m_startSize.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_startSize.getMaximumValue());
	thisEntry.append(SEP_HEAD).append(F_SIZE).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING, templ->m_startSizeRate.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_startSizeRate.getMaximumValue());
	thisEntry.append(SEP_HEAD).append(F_STARTSIZERATE).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING, templ->m_sizeRate.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_sizeRate.getMaximumValue());
	thisEntry.append(SEP_HEAD).append(F_SIZERATE).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING, templ->m_sizeRateDamping.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_sizeRateDamping.getMaximumValue());
	thisEntry.append(SEP_HEAD).append(F_SIZERATEDAMP).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);

	WWFormat::Into(buff1, FORMAT_STRING, templ->m_alphaKey[0].var.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_alphaKey[0].var.getMaximumValue());
	WWFormat::Into(buff3, "{:d}", templ->m_alphaKey[0].frame);
	thisEntry.append(SEP_HEAD).append(F_ALPHA1).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_EOL);

	WWFormat::Into(buff1, FORMAT_STRING, templ->m_alphaKey[1].var.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_alphaKey[1].var.getMaximumValue());
	WWFormat::Into(buff3, "{:d}", templ->m_alphaKey[1].frame);
	thisEntry.append(SEP_HEAD).append(F_ALPHA2).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_EOL);

	WWFormat::Into(buff1, FORMAT_STRING, templ->m_alphaKey[2].var.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_alphaKey[2].var.getMaximumValue());
	WWFormat::Into(buff3, "{:d}", templ->m_alphaKey[2].frame);
	thisEntry.append(SEP_HEAD).append(F_ALPHA3).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING, templ->m_alphaKey[3].var.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_alphaKey[3].var.getMaximumValue());
	WWFormat::Into(buff3, "{:d}", templ->m_alphaKey[3].frame);
	thisEntry.append(SEP_HEAD).append(F_ALPHA4).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING, templ->m_alphaKey[4].var.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_alphaKey[4].var.getMaximumValue());
	WWFormat::Into(buff3, "{:d}", templ->m_alphaKey[4].frame);
	thisEntry.append(SEP_HEAD).append(F_ALPHA5).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING, templ->m_alphaKey[5].var.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_alphaKey[5].var.getMaximumValue());
	WWFormat::Into(buff3, "{:d}", templ->m_alphaKey[5].frame);
	thisEntry.append(SEP_HEAD).append(F_ALPHA6).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING, templ->m_alphaKey[6].var.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_alphaKey[6].var.getMaximumValue());
	WWFormat::Into(buff3, "{:d}", templ->m_alphaKey[6].frame);
	thisEntry.append(SEP_HEAD).append(F_ALPHA7).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING, templ->m_alphaKey[7].var.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_alphaKey[7].var.getMaximumValue());
	WWFormat::Into(buff3, "{:d}", templ->m_alphaKey[7].frame);
	thisEntry.append(SEP_HEAD).append(F_ALPHA8).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_EOL);

	WWFormat::Into(buff1, "{}{:d}", STR_R.c_str(), REAL_TO_INT(templ->m_colorKey[0].color.red * 255 + 0.5));
	WWFormat::Into(buff2, "{}{:d}", STR_G.c_str(), REAL_TO_INT(templ->m_colorKey[0].color.green * 255 + 0.5));
	WWFormat::Into(buff3, "{}{:d}", STR_B.c_str(), REAL_TO_INT(templ->m_colorKey[0].color.blue * 255 + 0.5));
	WWFormat::Into(buff4, "{:d}", templ->m_colorKey[0].frame);
	thisEntry.append(SEP_HEAD).append(F_COLOR1).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_SPACE).append(buff4).append(SEP_EOL);

	WWFormat::Into(buff1, "{}{:d}", STR_R.c_str(), REAL_TO_INT(templ->m_colorKey[1].color.red * 255 + 0.5));
	WWFormat::Into(buff2, "{}{:d}", STR_G.c_str(), REAL_TO_INT(templ->m_colorKey[1].color.green * 255 + 0.5));
	WWFormat::Into(buff3, "{}{:d}", STR_B.c_str(), REAL_TO_INT(templ->m_colorKey[1].color.blue * 255 + 0.5));
	WWFormat::Into(buff4, "{:d}", templ->m_colorKey[1].frame);
	thisEntry.append(SEP_HEAD).append(F_COLOR2).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_SPACE).append(buff4).append(SEP_EOL);

	WWFormat::Into(buff1, "{}{:d}", STR_R.c_str(), REAL_TO_INT(templ->m_colorKey[2].color.red * 255 + 0.5));
	WWFormat::Into(buff2, "{}{:d}", STR_G.c_str(), REAL_TO_INT(templ->m_colorKey[2].color.green * 255 + 0.5));
	WWFormat::Into(buff3, "{}{:d}", STR_B.c_str(), REAL_TO_INT(templ->m_colorKey[2].color.blue * 255 + 0.5));
	WWFormat::Into(buff4, "{:d}", templ->m_colorKey[2].frame);
	thisEntry.append(SEP_HEAD).append(F_COLOR3).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_SPACE).append(buff4).append(SEP_EOL);
	
	WWFormat::Into(buff1, "{}{:d}", STR_R.c_str(), REAL_TO_INT(templ->m_colorKey[3].color.red * 255 + 0.5));
	WWFormat::Into(buff2, "{}{:d}", STR_G.c_str(), REAL_TO_INT(templ->m_colorKey[3].color.green * 255 + 0.5));
	WWFormat::Into(buff3, "{}{:d}", STR_B.c_str(), REAL_TO_INT(templ->m_colorKey[3].color.blue * 255 + 0.5));
	WWFormat::Into(buff4, "{:d}", templ->m_colorKey[3].frame);
	thisEntry.append(SEP_HEAD).append(F_COLOR4).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_SPACE).append(buff4).append(SEP_EOL);
	
	WWFormat::Into(buff1, "{}{:d}", STR_R.c_str(), REAL_TO_INT(templ->m_colorKey[4].color.red * 255 + 0.5));
	WWFormat::Into(buff2, "{}{:d}", STR_G.c_str(), REAL_TO_INT(templ->m_colorKey[4].color.green * 255 + 0.5));
	WWFormat::Into(buff3, "{}{:d}", STR_B.c_str(), REAL_TO_INT(templ->m_colorKey[4].color.blue * 255 + 0.5));
	WWFormat::Into(buff4, "{:d}", templ->m_colorKey[4].frame);
	thisEntry.append(SEP_HEAD).append(F_COLOR5).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_SPACE).append(buff4).append(SEP_EOL);
	
	WWFormat::Into(buff1, "{}{:d}", STR_R.c_str(), REAL_TO_INT(templ->m_colorKey[5].color.red * 255 + 0.5));
	WWFormat::Into(buff2, "{}{:d}", STR_G.c_str(), REAL_TO_INT(templ->m_colorKey[5].color.green * 255 + 0.5));
	WWFormat::Into(buff3, "{}{:d}", STR_B.c_str(), REAL_TO_INT(templ->m_colorKey[5].color.blue * 255 + 0.5));
	WWFormat::Into(buff4, "{:d}", templ->m_colorKey[5].frame);
	thisEntry.append(SEP_HEAD).append(F_COLOR6).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_SPACE).append(buff4).append(SEP_EOL);
	
	WWFormat::Into(buff1, "{}{:d}", STR_R.c_str(), REAL_TO_INT(templ->m_colorKey[6].color.red * 255 + 0.5));
	WWFormat::Into(buff2, "{}{:d}", STR_G.c_str(), REAL_TO_INT(templ->m_colorKey[6].color.green * 255 + 0.5));
	WWFormat::Into(buff3, "{}{:d}", STR_B.c_str(), REAL_TO_INT(templ->m_colorKey[6].color.blue * 255 + 0.5));
	WWFormat::Into(buff4, "{:d}", templ->m_colorKey[6].frame);
	thisEntry.append(SEP_HEAD).append(F_COLOR7).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_SPACE).append(buff4).append(SEP_EOL);
	
	WWFormat::Into(buff1, "{}{:d}", STR_R.c_str(), REAL_TO_INT(templ->m_colorKey[7].color.red * 255 + 0.5));
	WWFormat::Into(buff2, "{}{:d}", STR_G.c_str(), REAL_TO_INT(templ->m_colorKey[7].color.green * 255 + 0.5));
	WWFormat::Into(buff3, "{}{:d}", STR_B.c_str(), REAL_TO_INT(templ->m_colorKey[7].color.blue * 255 + 0.5));
	WWFormat::Into(buff4, "{:d}", templ->m_colorKey[7].frame);
	thisEntry.append(SEP_HEAD).append(F_COLOR8).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_SPACE).append(buff4).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING, templ->m_colorScale.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_colorScale.getMaximumValue());
	thisEntry.append(SEP_HEAD).append(F_COLORSCALE).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING, templ->m_burstDelay.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_burstDelay.getMaximumValue());
	thisEntry.append(SEP_HEAD).append(F_BURSTDELAY).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING, templ->m_burstCount.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_burstCount.getMaximumValue());
	thisEntry.append(SEP_HEAD).append(F_BURSTCOUNT).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING, templ->m_initialDelay.getMinimumValue());
	WWFormat::Into(buff2, FORMAT_STRING, templ->m_initialDelay.getMaximumValue());
	thisEntry.append(SEP_HEAD).append(F_INITIALDELAY).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	
	WWFormat::Into(buff1, FORMAT_STRING_LEADING_STRING, STR_X.c_str(), templ->m_driftVelocity.x);
	WWFormat::Into(buff2, FORMAT_STRING_LEADING_STRING, STR_Y.c_str(), templ->m_driftVelocity.y);
	WWFormat::Into(buff3, FORMAT_STRING_LEADING_STRING, STR_Z.c_str(), templ->m_driftVelocity.z);
	thisEntry.append(SEP_HEAD).append(F_DRIFTVELOCITY).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_EOL);

	thisEntry.append(SEP_HEAD).append(F_VELOCITYTYPE).append(EQ_WITH_SPACES).append(EmissionVelocityTypeNames[templ->m_emissionVelocityType]).append(SEP_EOL);

	if (templ->m_emissionVelocityType == ParticleSystemInfo::ORTHO) {

		WWFormat::Into(buff1, FORMAT_STRING, templ->m_emissionVelocity.ortho.x.getMinimumValue());
		WWFormat::Into(buff2, FORMAT_STRING, templ->m_emissionVelocity.ortho.x.getMaximumValue());
		thisEntry.append(SEP_HEAD).append(F_VELORTHOX).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
		
		WWFormat::Into(buff1, FORMAT_STRING, templ->m_emissionVelocity.ortho.y.getMinimumValue());
		WWFormat::Into(buff2, FORMAT_STRING, templ->m_emissionVelocity.ortho.y.getMaximumValue());
		thisEntry.append(SEP_HEAD).append(F_VELORTHOY).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
		
		WWFormat::Into(buff1, FORMAT_STRING, templ->m_emissionVelocity.ortho.z.getMinimumValue());
		WWFormat::Into(buff2, FORMAT_STRING, templ->m_emissionVelocity.ortho.z.getMaximumValue());
		thisEntry.append(SEP_HEAD).append(F_VELORTHOZ).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	} else if (templ->m_emissionVelocityType == ParticleSystemInfo::SPHERICAL) {
		WWFormat::Into(buff1, FORMAT_STRING, templ->m_emissionVelocity.spherical.speed.getMinimumValue());
		WWFormat::Into(buff2, FORMAT_STRING, templ->m_emissionVelocity.spherical.speed.getMaximumValue());
		thisEntry.append(SEP_HEAD).append(F_VELSPHERE).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);

	} else if (templ->m_emissionVelocityType == ParticleSystemInfo::HEMISPHERICAL) {
		WWFormat::Into(buff1, FORMAT_STRING, templ->m_emissionVelocity.hemispherical.speed.getMinimumValue());
		WWFormat::Into(buff2, FORMAT_STRING, templ->m_emissionVelocity.hemispherical.speed.getMaximumValue());
		thisEntry.append(SEP_HEAD).append(F_HEMISPHERE).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);

	} else if (templ->m_emissionVelocityType == ParticleSystemInfo::CYLINDRICAL) {
		WWFormat::Into(buff1, FORMAT_STRING, templ->m_emissionVelocity.cylindrical.radial.getMinimumValue());
		WWFormat::Into(buff2, FORMAT_STRING, templ->m_emissionVelocity.cylindrical.radial.getMaximumValue());
		thisEntry.append(SEP_HEAD).append(F_VELCYLRAD).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);

		WWFormat::Into(buff1, FORMAT_STRING, templ->m_emissionVelocity.cylindrical.normal.getMinimumValue());
		WWFormat::Into(buff2, FORMAT_STRING, templ->m_emissionVelocity.cylindrical.normal.getMaximumValue());
		thisEntry.append(SEP_HEAD).append(F_VELCYLNOR).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);

	} else if (templ->m_emissionVelocityType == ParticleSystemInfo::OUTWARD) {
		WWFormat::Into(buff1, FORMAT_STRING, templ->m_emissionVelocity.outward.speed.getMinimumValue());
		WWFormat::Into(buff2, FORMAT_STRING, templ->m_emissionVelocity.outward.speed.getMaximumValue());
		thisEntry.append(SEP_HEAD).append(F_VELOUTWARD).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);

		WWFormat::Into(buff1, FORMAT_STRING, templ->m_emissionVelocity.outward.otherSpeed.getMinimumValue());
		WWFormat::Into(buff2, FORMAT_STRING, templ->m_emissionVelocity.outward.otherSpeed.getMaximumValue());
		thisEntry.append(SEP_HEAD).append(F_VELOUTOTHER).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_EOL);
	}	

	thisEntry.append(SEP_HEAD).append(F_VOLUMETYPE).append(EQ_WITH_SPACES).append(EmissionVolumeTypeNames[templ->m_emissionVolumeType]).append(SEP_EOL);

	if (templ->m_emissionVolumeType == ParticleSystemInfo::POINT) {
		// nothing to output here for lines
	} else if (templ->m_emissionVolumeType == ParticleSystemInfo::LINE) {
		WWFormat::Into(buff1, FORMAT_STRING_LEADING_STRING, STR_X.c_str(), templ->m_emissionVolume.line.start.x);
		WWFormat::Into(buff2, FORMAT_STRING_LEADING_STRING, STR_Y.c_str(), templ->m_emissionVolume.line.start.y);
		WWFormat::Into(buff3, FORMAT_STRING_LEADING_STRING, STR_Z.c_str(), templ->m_emissionVolume.line.start.z);
		thisEntry.append(SEP_HEAD).append(F_VOLLINESTART).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_EOL);

		WWFormat::Into(buff1, FORMAT_STRING_LEADING_STRING, STR_X.c_str(), templ->m_emissionVolume.line.end.x);
		WWFormat::Into(buff2, FORMAT_STRING_LEADING_STRING, STR_Y.c_str(), templ->m_emissionVolume.line.end.y);
		WWFormat::Into(buff3, FORMAT_STRING_LEADING_STRING, STR_Z.c_str(), templ->m_emissionVolume.line.end.z);
		thisEntry.append(SEP_HEAD).append(F_VOLLINEEND).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_EOL);

	} else if (templ->m_emissionVolumeType == ParticleSystemInfo::BOX) {
		WWFormat::Into(buff1, FORMAT_STRING_LEADING_STRING, STR_X.c_str(), templ->m_emissionVolume.box.halfSize.x);
		WWFormat::Into(buff2, FORMAT_STRING_LEADING_STRING, STR_Y.c_str(), templ->m_emissionVolume.box.halfSize.y);
		WWFormat::Into(buff3, FORMAT_STRING_LEADING_STRING, STR_Z.c_str(), templ->m_emissionVolume.box.halfSize.z);
		thisEntry.append(SEP_HEAD).append(F_VOLBOXHALF).append(EQ_WITH_SPACES).append(buff1).append(SEP_SPACE).append(buff2).append(SEP_SPACE).append(buff3).append(SEP_EOL);

	} else if (templ->m_emissionVolumeType == ParticleSystemInfo::SPHERE) {
		WWFormat::Into(buff1, FORMAT_STRING, templ->m_emissionVolume.sphere.radius);
		thisEntry.append(SEP_HEAD).append(F_VOLSPHERERAD).append(EQ_WITH_SPACES).append(buff1).append(SEP_EOL);

	} else if (templ->m_emissionVolumeType == ParticleSystemInfo::CYLINDER) {
		WWFormat::Into(buff1, FORMAT_STRING, templ->m_emissionVolume.cylinder.radius);
		thisEntry.append(SEP_HEAD).append(F_VOLCYLRAD).append(EQ_WITH_SPACES).append(buff1).append(SEP_EOL);

		WWFormat::Into(buff1, FORMAT_STRING, templ->m_emissionVolume.cylinder.length);
		thisEntry.append(SEP_HEAD).append(F_VOLCYLLEN).append(EQ_WITH_SPACES).append(buff1).append(SEP_EOL);
	}

	thisEntry.append(SEP_HEAD).append(F_ISHOLLOW).append(EQ_WITH_SPACES).append((templ->m_isEmissionVolumeHollow ? STR_TRUE : STR_FALSE)).append(SEP_EOL);
	thisEntry.append(SEP_HEAD).append(F_ISXYPLANAR).append(EQ_WITH_SPACES).append((templ->m_isGroundAligned ? STR_TRUE : STR_FALSE)).append(SEP_EOL);
	thisEntry.append(SEP_HEAD).append(F_ISEMITABOVEGROUNDONLY).append(EQ_WITH_SPACES).append((templ->m_isEmitAboveGroundOnly ? STR_TRUE : STR_FALSE)).append(SEP_EOL);
	thisEntry.append(SEP_HEAD).append(F_ISPARTICLEUPTOWARDSEMITTER).append(EQ_WITH_SPACES).append((templ->m_isParticleUpTowardsEmitter ? STR_TRUE : STR_FALSE)).append(SEP_EOL);

	// wind angle and stuff
	thisEntry.append(SEP_HEAD).append(F_WINDMOTION).append(EQ_WITH_SPACES).append(WindMotionNames[templ->m_windMotion]).append(SEP_EOL);

	WWFormat::Into(buff1, "{:f}", templ->m_windAngleChangeMin);
	thisEntry.append(SEP_HEAD).append(F_WINDANGLECHANGEMIN).append(EQ_WITH_SPACES).append(buff1).append(SEP_EOL);
	WWFormat::Into(buff1, "{:f}", templ->m_windAngleChangeMax);
	thisEntry.append(SEP_HEAD).append(F_WINDANGLECHANGEMAX).append(EQ_WITH_SPACES).append(buff1).append(SEP_EOL);

	WWFormat::Into(buff1, "{:f}", templ->m_windMotionStartAngleMin);
	thisEntry.append(SEP_HEAD).append(F_WINDPINGPONGSTARTANGLEMIN).append(EQ_WITH_SPACES).append(buff1).append(SEP_EOL);
	WWFormat::Into(buff1, "{:f}", templ->m_windMotionStartAngleMax);
	thisEntry.append(SEP_HEAD).append(F_WINDPINGPONGSTARTANGLEMAX).append(EQ_WITH_SPACES).append(buff1).append(SEP_EOL);

	WWFormat::Into(buff1, "{:f}", templ->m_windMotionEndAngleMin);
	thisEntry.append(SEP_HEAD).append(F_WINDPINGPONGENDANGLEMIN).append(EQ_WITH_SPACES).append(buff1).append(SEP_EOL);
	WWFormat::Into(buff1, "{:f}", templ->m_windMotionEndAngleMax);
	thisEntry.append(SEP_HEAD).append(F_WINDPINGPONGENDANGLEMAX).append(EQ_WITH_SPACES).append(buff1).append(SEP_EOL);

	thisEntry.append(STR_END).append(SEP_EOL).append(SEP_EOL);

//	fwrite(thisEntry.c_str(), thisEntry.size(), 1, out);
	out->write(thisEntry.c_str(), static_cast<Int>(thisEntry.size()));
}
