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

#define DEFINE_W3DANIMMODE_NAMES
#define DEFINE_WEAPONSLOTTYPE_NAMES

#define NO_DEBUG_CRC

#include "Common/crc.h"
#include "formatinto.h"
#include "Common/CRCDebug.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/RandomValue.h"
#include "Common/ThingTemplate.h"
#include "Common/GameLOD.h"
#include "Common/Xfer.h"
#include "Common/GameState.h"
#include "Common/QuickTrig.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/Shadow.h"
#include "GameLogic/GameLogic.h"		// for real-time frame
#include "GameLogic/Object.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "WW3D2/Animation/hanim.h"
#include "WW3D2/Mesh/hlod.h"
#include "WW3D2/Mesh/rendobj.h"
#include "WW3D2/Mesh/mesh.h"
#include "WW3D2/Mesh/meshmdl.h"
#include "Common/BitFlagsIO.h"
#include "GameClient/GameClient.h"	// isRenderOnlyFrame()
#include "Lib/SafeString.h"
#include "W3DModelDrawInternal.h"

// Module data: INI parsing of the condition states

// flags that aren't read directly from INI, but set in response to various other situations
enum INIReadFlagsType
{
	ANIMS_COPIED_FROM_DEFAULT_STATE = 0,
	GOT_NONIDLE_ANIMS,
	GOT_IDLE_ANIMS,
};

//-------------------------------------------------------------------------------------------------
W3DModelDrawModuleData::W3DModelDrawModuleData() : 
	m_validated(0),
	m_okToChangeModelColor(false),
  m_particlesAttachedToAnimatedBones(false),
	m_animationsRequirePower(true),
#ifdef CACHE_ATTACH_BONE
	m_attachToDrawableBoneOffsetValid(false),
#endif
	m_minLODRequired(STATIC_GAME_LOD_LOW),
	m_defaultState(-1)
{
	const Real MAX_SHIFT = 3.0f;			
	const Real INITIAL_RECOIL_RATE = 2.0f;	
	const Real RECOIL_DAMPING = 0.4f;	
	const Real SETTLE_RATE = 0.065f;		

	m_projectileBoneFeedbackEnabledSlots = 0;
	m_initialRecoil = INITIAL_RECOIL_RATE;
	m_maxRecoil = MAX_SHIFT;
	m_recoilDamping = RECOIL_DAMPING;
	m_recoilSettle = SETTLE_RATE;


  m_receivesDynamicLights = TRUE;

	// m_ignoreConditionStates defaults to all zero, which is what we want
}

//-------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::validateStuffForTimeAndWeather(const Drawable* draw, Bool night, Bool snowy) const
{
	if (!isValidTimeToCalcLogicStuff())
		return;

	enum
	{
		NORMAL			= 0x0001,
		NIGHT				= 0x0002,
		SNOWY				= 0x0004,
		NIGHT_SNOWY	= 0x0008
	};

	Int mode;
	if (night)
	{
		mode = (snowy) ? NIGHT_SNOWY : NIGHT;
	}
	else
	{
		mode = (snowy) ? SNOWY : NORMAL;
	}

	if (m_validated & mode)
		return;

	m_validated |= mode;

	for (ModelConditionVector::iterator c_it = m_conditionStates.begin(); c_it != m_conditionStates.end(); ++c_it)
	{
		if (!c_it->matchesMode(false, false) && !c_it->matchesMode(night, snowy))
			continue;

		c_it->validateStuff(nullptr, draw->getScale(), m_extraPublicBones);
	}

	for (TransitionMap::iterator t_it = m_transitionMap.begin(); t_it != m_transitionMap.end(); ++t_it)
	{
		// here's the tricky part: only want to load the anims for this one if there is at least one
		// source AND at least one dest state that matches the current mode
		NameKeyType src = recoverSrcState(t_it->first);
		NameKeyType dst = recoverDstState(t_it->first);

		Bool a = false;
		Bool b = false;
		for (ModelConditionVector::iterator c_it = m_conditionStates.begin(); c_it != m_conditionStates.end(); ++c_it)
		{

			if (!a && c_it->m_transitionKey == src && c_it->matchesMode(night, snowy))
				a = true;

			if (!b && c_it->m_transitionKey == dst && c_it->matchesMode(night, snowy))
				b = true;

		}
		if (a && b)
		{
			t_it->second.loadAnimations();
			// nope -- transition states don't get public bones.
			//it->addPublicBone(m_extraPublicBones);

		// srj sez: hm, this doesn't make sense; I think we really do need to validate transition states.
			t_it->second.validateStuff(nullptr, draw->getScale(), m_extraPublicBones);
		}
	}
}

//-------------------------------------------------------------------------------------------------
W3DModelDrawModuleData::~W3DModelDrawModuleData()
{
	m_conditionStateMap.clear();
}

//-------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::preloadAssets( TimeOfDay timeOfDay, Real scale ) const
{

	for( ModelConditionVector::iterator it = m_conditionStates.begin(); 
			 it != m_conditionStates.end(); 
			 ++it )
	{

		it->preloadAssets( timeOfDay, scale );

	}

}

//-------------------------------------------------------------------------------------------------
AsciiString W3DModelDrawModuleData::getBestModelNameForWB(const ModelConditionFlags& c) const
{
	const ModelConditionInfo* info = findBestInfo(c);
	if (info)
		return info->m_modelName;
	return AsciiString::TheEmptyString;
}

#ifdef CACHE_ATTACH_BONE
//-------------------------------------------------------------------------------------------------
const Vector3* W3DModelDrawModuleData::getAttachToDrawableBoneOffset(const Drawable* draw) const
{
	if (m_attachToDrawableBone.isEmpty())
	{
		return nullptr;
	}
	else
	{
		if (!m_attachToDrawableBoneOffsetValid)
		{
		// must use pristine bone here since the result is used by logic
			Matrix3D boneMtx;
			if (draw->getPristineBonePositions(m_attachToDrawableBone.str(), 0, nullptr, &boneMtx, 1) == 1)
			{
				m_attachToDrawableBoneOffset = boneMtx.Get_Translation();
			}
			else
			{
				m_attachToDrawableBoneOffset.X = 0;
				m_attachToDrawableBoneOffset.Y = 0;
				m_attachToDrawableBoneOffset.Z = 0;
			}
			m_attachToDrawableBoneOffsetValid = true;
		}
		return &m_attachToDrawableBoneOffset;
	}
}
#endif

static const char *ACBitsNames[] =
{
	"RANDOMSTART",
	"START_FRAME_FIRST",
	"START_FRAME_LAST",
	"ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT",
	"PRISTINE_BONE_POS_IN_FINAL_FRAME",
	"MAINTAIN_FRAME_ACROSS_STATES",
	"RESTART_ANIM_WHEN_COMPLETE",
	"MAINTAIN_FRAME_ACROSS_STATES2",
	"MAINTAIN_FRAME_ACROSS_STATES3",
	"MAINTAIN_FRAME_ACROSS_STATES4",
	
	nullptr
};

//-------------------------------------------------------------------------------------------------
enum class ParseCondStateType
{
	PARSE_NORMAL,
	PARSE_DEFAULT,
	PARSE_TRANSITION,
	PARSE_ALIAS
};

//-------------------------------------------------------------------------------------------------
static void parseAsciiStringLC( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	AsciiString* asciiString = static_cast<AsciiString *>(store);
	*asciiString = ini->getNextAsciiString();
	asciiString->toLower();
}

//-------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::buildFieldParse(MultiIniFieldParse& p) 
{
  ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] = 
	{
		{ "InitialRecoilSpeed",	INI::parseVelocityReal, nullptr, offsetof(W3DModelDrawModuleData, m_initialRecoil) },
		{ "MaxRecoilDistance",	INI::parseReal, nullptr, offsetof(W3DModelDrawModuleData, m_maxRecoil) },
		{ "RecoilDamping",	INI::parseReal, nullptr, offsetof(W3DModelDrawModuleData, m_recoilDamping) },
		{ "RecoilSettleSpeed",	INI::parseVelocityReal, nullptr, offsetof(W3DModelDrawModuleData, m_recoilSettle) },
		{ "OkToChangeModelColor",	INI::parseBool, nullptr, offsetof(W3DModelDrawModuleData, m_okToChangeModelColor) },
		{ "AnimationsRequirePower",	INI::parseBool, nullptr, offsetof(W3DModelDrawModuleData, m_animationsRequirePower) },
		{ "ParticlesAttachedToAnimatedBones",	INI::parseBool, nullptr, offsetof(W3DModelDrawModuleData, m_particlesAttachedToAnimatedBones) },
		{ "MinLODRequired",		INI::parseStaticGameLODLevel,	nullptr,	offsetof(W3DModelDrawModuleData, m_minLODRequired) },
		{ "ProjectileBoneFeedbackEnabledSlots", INI::parseBitString32, TheWeaponSlotTypeNames, offsetof(W3DModelDrawModuleData, m_projectileBoneFeedbackEnabledSlots) },
		{ "DefaultConditionState", W3DModelDrawModuleData::parseConditionState, reinterpret_cast<void*>(ParseCondStateType::PARSE_DEFAULT), 0 },
		{ "ConditionState", W3DModelDrawModuleData::parseConditionState, reinterpret_cast<void*>(ParseCondStateType::PARSE_NORMAL), 0 },
		{ "AliasConditionState", W3DModelDrawModuleData::parseConditionState, reinterpret_cast<void*>(ParseCondStateType::PARSE_ALIAS), 0 },
		{ "TransitionState", W3DModelDrawModuleData::parseConditionState, reinterpret_cast<void*>(ParseCondStateType::PARSE_TRANSITION), 0 },
		{ "TrackMarks", parseAsciiStringLC, nullptr, offsetof(W3DModelDrawModuleData, m_trackFile) },
		{ "ExtraPublicBone", INI::parseAsciiStringVectorAppend, nullptr, offsetof(W3DModelDrawModuleData, m_extraPublicBones) },
		{ "AttachToBoneInAnotherModule", parseAsciiStringLC, nullptr, offsetof(W3DModelDrawModuleData, m_attachToDrawableBone) },
		{ "IgnoreConditionStates", ModelConditionFlags::parseFromINI, nullptr, offsetof(W3DModelDrawModuleData, m_ignoreConditionStates) },
		{ "ReceivesDynamicLights", INI::parseBool, nullptr, offsetof(W3DModelDrawModuleData, m_receivesDynamicLights) },
    { 0, 0, 0, 0 }
	};
  p.add(dataFieldParse);

}

//-------------------------------------------------------------------------------------------------
enum class AnimParseType
{
	ANIM_NORMAL,
	ANIM_IDLE
};

//-------------------------------------------------------------------------------------------------
static void parseAnimation(INI* ini, void *instance, void * /*store*/, const void* userData)
{
	AnimParseType animType = static_cast<AnimParseType>(reinterpret_cast<uintptr_t>(userData));	// pointer-sized

	AsciiString animName = ini->getNextAsciiString();
	animName.toLower();

	const char* distanceCoveredToken = ini->getNextTokenOrNull();
	Real distanceCovered = distanceCoveredToken ? INI::scanReal(distanceCoveredToken) : 0;
	DEBUG_ASSERTCRASH(!(animType == AnimParseType::ANIM_IDLE && distanceCovered != 0), ("You should not specify nonzero DistanceCovered values for Idle Anims"));

	const char* timesToRepeatToken = ini->getNextTokenOrNull();
	Int timesToRepeat = timesToRepeatToken ? INI::scanInt(timesToRepeatToken) : 1;
	if (timesToRepeat < 1) timesToRepeat = 1;

	W3DAnimationInfo animInfo(animName, (animType == AnimParseType::ANIM_IDLE), distanceCovered);

	ModelConditionInfo* self = static_cast<ModelConditionInfo*>(instance);
	if (self->m_iniReadFlags & (1<<ANIMS_COPIED_FROM_DEFAULT_STATE))
	{
		self->m_iniReadFlags &= ~((1<<ANIMS_COPIED_FROM_DEFAULT_STATE)|(1<<GOT_IDLE_ANIMS)|(1<<GOT_NONIDLE_ANIMS));
		self->m_animations.clear();
	}

	if (animInfo.isIdleAnim())
		self->m_iniReadFlags |= (1<<GOT_IDLE_ANIMS);
	else
		self->m_iniReadFlags |= (1<<GOT_NONIDLE_ANIMS);

	if (!animName.isEmpty() && !animName.isNone())
	{
		while (timesToRepeat--)
			self->m_animations.push_back(animInfo);
	}
}	

//-------------------------------------------------------------------------------------------------
static void parseShowHideSubObject(INI* ini, void *instance, void *store, const void* userData)
{
	std::vector<ModelConditionInfo::HideShowSubObjInfo>* vec = static_cast<std::vector<ModelConditionInfo::HideShowSubObjInfo>*>(store);
	AsciiString subObjName = ini->getNextAsciiString();
	subObjName.toLower();

	// handle "None"
	if (subObjName.isNone())
	{
		vec->clear();
		return;
	}

	while (subObjName.isNotEmpty())
	{
		Bool found = false;
		for (std::vector<ModelConditionInfo::HideShowSubObjInfo>::iterator it = vec->begin(); it != vec->end(); ++it)
		{
			if (stricmp(it->subObjName.str(), subObjName.str()) == 0)
			{
				it->hide = (userData != nullptr);
				found = true;
			}
		}
		
		if (!found)
		{
			ModelConditionInfo::HideShowSubObjInfo info;
			info.subObjName = subObjName;
			info.hide = (userData != nullptr);
			vec->push_back(info);
		}
		subObjName = ini->getNextAsciiString();
		subObjName.toLower();
	}
}	

//-------------------------------------------------------------------------------------------------
static void parseWeaponBoneName(INI* ini, void *instance, void * store, const void* /*userData*/)
{
	ModelConditionInfo* self = static_cast<ModelConditionInfo*>(instance);
	AsciiString* arr = static_cast<AsciiString*>(store);
	
	WeaponSlotType wslot = static_cast<WeaponSlotType>(INI::scanIndexList(ini->getNextToken(), TheWeaponSlotTypeNames));
	arr[wslot] = ini->getNextAsciiString();
	arr[wslot].toLower();
	if (arr[wslot].isNone())
		arr[wslot].clear();

	if (self)
		self->addPublicBone(arr[wslot]);
}

//-------------------------------------------------------------------------------------------------
static void parseParticleSysBone(INI* ini, void *instance, void * store, const void * /*userData*/)
{
	ParticleSysBoneInfo info;
	info.boneName = ini->getNextAsciiString();
	info.boneName.toLower();
	ini->parseParticleSystemTemplate(ini, instance, &(info.particleSystemTemplate), nullptr);
	ModelConditionInfo *self = static_cast<ModelConditionInfo *>(instance);
	self->m_particleSysBones.push_back(info);
}

//-------------------------------------------------------------------------------------------------
static void parseRealRange( INI *ini, void *instance, void *store, const void* /*userData*/ )
{
	ModelConditionInfo *self = static_cast<ModelConditionInfo *>(instance);

	const char *token = ini->getNextToken();
	self->m_animMinSpeedFactor = ini->scanReal( token );
	token = ini->getNextToken();
	self->m_animMaxSpeedFactor = ini->scanReal( token );
}

//-------------------------------------------------------------------------------------------------
static void parseLowercaseNameKey(INI* ini, void *instance, void * store, const void * /*userData*/)
{
	NameKeyType* key = static_cast<NameKeyType*>(store);

	AsciiString tmp = ini->getNextToken();
	tmp.toLower();

	*key = NAMEKEY(tmp.str());
}

//-------------------------------------------------------------------------------------------------
static void parseBoneNameKey(INI* ini, void *instance, void * store, const void * /*userData*/)
{
	ModelConditionInfo* self = static_cast<ModelConditionInfo*>(instance);
	NameKeyType* key = static_cast<NameKeyType*>(store);

	AsciiString tmp = ini->getNextToken();
	tmp.toLower();

	if (self)
		self->addPublicBone(tmp);

	if (tmp.isEmpty() || tmp.isNone())
		*key = NAMEKEY_INVALID;
	else
		*key = NAMEKEY(tmp.str());
}

//-------------------------------------------------------------------------------------------------
static Bool doesStateExist(const ModelConditionVector& v, const ModelConditionFlags& f)
{
	for (ModelConditionVector::const_iterator it = v.begin(); it != v.end(); ++it)
	{
		for (Int i = it->getConditionsYesCount()-1; i >= 0; --i)
		{
			if (f == it->getNthConditionsYes(i))
				return true;
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::parseConditionState(INI* ini, void *instance, void * /*store*/, const void* userData)
{
	static const FieldParse myFieldParse[] = 
	{
		{ "Model",	parseAsciiStringLC, nullptr, offsetof(ModelConditionInfo, m_modelName) },
		{ "Turret",	parseBoneNameKey, nullptr, offsetof(ModelConditionInfo, m_turrets[0].m_turretAngleNameKey) },
		{ "TurretArtAngle", INI::parseAngleReal, nullptr, offsetof(ModelConditionInfo, m_turrets[0].m_turretArtAngle) },
		{ "TurretPitch",	parseBoneNameKey, nullptr, offsetof(ModelConditionInfo, m_turrets[0].m_turretPitchNameKey) },
		{ "TurretArtPitch", INI::parseAngleReal, nullptr, offsetof(ModelConditionInfo, m_turrets[0].m_turretArtPitch) },
		{ "AltTurret",	parseBoneNameKey, nullptr, offsetof(ModelConditionInfo, m_turrets[1].m_turretAngleNameKey) },
		{ "AltTurretArtAngle", INI::parseAngleReal, nullptr, offsetof(ModelConditionInfo, m_turrets[1].m_turretArtAngle) },
		{ "AltTurretPitch",	parseBoneNameKey, nullptr, offsetof(ModelConditionInfo, m_turrets[1].m_turretPitchNameKey) },
		{ "AltTurretArtPitch", INI::parseAngleReal, nullptr, offsetof(ModelConditionInfo, m_turrets[1].m_turretArtPitch) },
		{ "ShowSubObject", parseShowHideSubObject, static_cast<void*>(0), offsetof(ModelConditionInfo, m_hideShowVec) },
		{ "HideSubObject", parseShowHideSubObject, reinterpret_cast<void*>(1), offsetof(ModelConditionInfo, m_hideShowVec) },
		{ "WeaponFireFXBone", parseWeaponBoneName, nullptr, offsetof(ModelConditionInfo, m_weaponFireFXBoneName[0]) },
		{ "WeaponRecoilBone", parseWeaponBoneName, nullptr, offsetof(ModelConditionInfo, m_weaponRecoilBoneName[0]) },
		{ "WeaponMuzzleFlash", parseWeaponBoneName, nullptr, offsetof(ModelConditionInfo, m_weaponMuzzleFlashName[0]) },
		{ "WeaponLaunchBone", parseWeaponBoneName, nullptr, offsetof(ModelConditionInfo, m_weaponProjectileLaunchBoneName[0]) },
		{ "WeaponHideShowBone", parseWeaponBoneName, nullptr, offsetof(ModelConditionInfo, m_weaponProjectileHideShowName[0]) },
		{ "Animation", parseAnimation, reinterpret_cast<void*>(AnimParseType::ANIM_NORMAL), offsetof(ModelConditionInfo, m_animations) },
		{ "IdleAnimation", parseAnimation, reinterpret_cast<void*>(AnimParseType::ANIM_IDLE), offsetof(ModelConditionInfo, m_animations) },
		{ "AnimationMode", INI::parseIndexList, TheAnimModeNames, offsetof(ModelConditionInfo, m_mode) },
		{ "TransitionKey", parseLowercaseNameKey, nullptr, offsetof(ModelConditionInfo, m_transitionKey) },
		{ "WaitForStateToFinishIfPossible", parseLowercaseNameKey, nullptr, offsetof(ModelConditionInfo, m_allowToFinishKey) },
		{ "Flags", INI::parseBitString32, ACBitsNames, offsetof(ModelConditionInfo, m_flags) },
		{ "ParticleSysBone", parseParticleSysBone, nullptr, 0 },
		{ "AnimationSpeedFactorRange", parseRealRange, nullptr, 0 },
		{ 0, 0, 0, 0 }
	};

	ModelConditionInfo info;
	W3DModelDrawModuleData* self = static_cast<W3DModelDrawModuleData*>(instance);
	ParseCondStateType cst = static_cast<ParseCondStateType>(reinterpret_cast<uintptr_t>(userData));	// pointer-sized
	switch (cst)
	{
		case ParseCondStateType::PARSE_DEFAULT:
		{
			if (self->m_defaultState >= 0)
			{
				DEBUG_CRASH(("*** ASSET ERROR: you may have only one default state!\n"));
				throw INI_INVALID_DATA;
			}
			else if (ini->getNextTokenOrNull())
			{
				DEBUG_CRASH(("*** ASSET ERROR: unknown keyword\n"));
				throw INI_INVALID_DATA;
			}
			else
			{
				if (!self->m_conditionStates.empty())
				{
					DEBUG_CRASH(("*** ASSET ERROR: when using DefaultConditionState, it must be the first state listed (%s)\n",TheThingTemplateBeingParsedName.str()));
					throw INI_INVALID_DATA;
				}

				// note, this is size(), not size()-1, since we haven't actually modified the list yet
				self->m_defaultState = static_cast<Int>(self->m_conditionStates.size());
				//DEBUG_LOG(("set default state to %d\n",self->m_defaultState));

				// add an empty conditionstateflag set
				ModelConditionFlags blankConditions;
				info.m_conditionsYesVec.clear();
				info.m_conditionsYesVec.push_back(blankConditions);

	#if defined(_DEBUG)
				info.m_description.clear();
				info.m_description.concat(TheThingTemplateBeingParsedName);
				info.m_description.concat(" DEFAULT");
	#endif
			}
		}
		break;

		case ParseCondStateType::PARSE_TRANSITION:
		{
		  AsciiString firstNm = ini->getNextToken(); firstNm.toLower();
		  AsciiString secondNm = ini->getNextToken(); secondNm.toLower();
			NameKeyType firstKey = NAMEKEY(firstNm);
			NameKeyType secondKey = NAMEKEY(secondNm);

			if (firstKey == secondKey)
			{
				DEBUG_CRASH(("*** ASSET ERROR: You may not declare a transition between two identical states\n"));
				throw INI_INVALID_DATA;
			}

			if (self->m_defaultState >= 0)
			{
				info = self->m_conditionStates.at(self->m_defaultState);
				info.m_iniReadFlags |= (1<<ANIMS_COPIED_FROM_DEFAULT_STATE);
				info.m_transitionKey = NAMEKEY_INVALID;
				info.m_allowToFinishKey = NAMEKEY_INVALID;
			}

			info.m_transitionSig = buildTransitionSig(firstKey, secondKey);
	#if defined(_DEBUG)
			info.m_description.clear();
			info.m_description.concat(TheThingTemplateBeingParsedName);
			info.m_description.concat(" TRANSITION: ");
			info.m_description.concat(firstNm);
			info.m_description.concat(" ");
			info.m_description.concat(secondNm);
	#endif

		}
		break;

		case ParseCondStateType::PARSE_ALIAS:
		{
			if (self->m_conditionStates.empty())
			{
				DEBUG_CRASH(("*** ASSET ERROR: AliasConditionState must refer to the previous state!\n"));
				throw INI_INVALID_DATA;
			}
			
			ModelConditionInfo& prevState = self->m_conditionStates.at(self->m_conditionStates.size()-1);
			
			ModelConditionFlags conditionsYes;

	#if defined(_DEBUG)
			AsciiString description;
			conditionsYes.parse(ini, &description);

			prevState.m_description.concat("\nAKA: ");
			prevState.m_description.concat(description);
	#else
			conditionsYes.parse(ini, nullptr);
	#endif
			
			if (conditionsYes.anyIntersectionWith(self->m_ignoreConditionStates))
			{
				DEBUG_CRASH(("You should not specify bits in a state once they are used in IgnoreConditionStates (%s)\n", TheThingTemplateBeingParsedName.str()));
				throw INI_INVALID_DATA;
			}

			if (doesStateExist(self->m_conditionStates, conditionsYes))
			{
				DEBUG_CRASH(("*** ASSET ERROR: duplicate condition states are not currently allowed"));
				throw INI_INVALID_DATA;
			}


			if (!conditionsYes.any() && self->m_defaultState >= 0)
			{
				DEBUG_CRASH(("*** ASSET ERROR: you may not specify both a Default state and a Conditions=None state"));
				throw INI_INVALID_DATA;
			}

			prevState.m_conditionsYesVec.push_back(conditionsYes);
			
			// yes, return, NOT break!
			return;
		}

		case ParseCondStateType::PARSE_NORMAL:
		{
			if (self->m_defaultState >= 0 && cst != ParseCondStateType::PARSE_ALIAS)
			{
				info = self->m_conditionStates.at(self->m_defaultState);
				info.m_iniReadFlags |= (1<<ANIMS_COPIED_FROM_DEFAULT_STATE);
				info.m_conditionsYesVec.clear();
			}
	// no, we do not currently require a default state, cuz it would break the exiting INI
	// files too badly. maybe someday.
	//	else
	//	{
	//		DEBUG_CRASH(("*** ASSET ERROR: you must specify a default state\n"));
	//		throw INI_INVALID_DATA;
	//	}
			
			ModelConditionFlags conditionsYes;
	#if defined(_DEBUG)
			AsciiString description;
			conditionsYes.parse(ini, &description);

			info.m_description.clear();
			info.m_description.concat(TheThingTemplateBeingParsedName);
			info.m_description.concat("\n         ");
			info.m_description.concat(description);
	#else
			conditionsYes.parse(ini, nullptr);
	#endif

			if (conditionsYes.anyIntersectionWith(self->m_ignoreConditionStates))
			{
				DEBUG_CRASH(("You should not specify bits in a state once they are used in IgnoreConditionStates (%s)\n", TheThingTemplateBeingParsedName.str()));
				throw INI_INVALID_DATA;
			}

			if (self->m_defaultState < 0 && self->m_conditionStates.empty() && conditionsYes.any())
			{
				// it doesn't actually NEED to be first, but it does need to be present, and this is the simplest way to enforce...
				DEBUG_CRASH(("*** ASSET ERROR: when not using DefaultConditionState, the first ConditionState must be for NONE (%s)\n",TheThingTemplateBeingParsedName.str()));
				throw INI_INVALID_DATA;
			}

			if (!conditionsYes.any() && self->m_defaultState >= 0)
			{
				DEBUG_CRASH(("*** ASSET ERROR: you may not specify both a Default state and a Conditions=None state"));
				throw INI_INVALID_DATA;
			}

			if (doesStateExist(self->m_conditionStates, conditionsYes))
			{
				DEBUG_CRASH(("*** ASSET ERROR: duplicate condition states are not currently allowed (%s)",info.m_description.str()));
				throw INI_INVALID_DATA;
			}
			
			DEBUG_ASSERTCRASH(info.m_conditionsYesVec.size() == 0, ("*** ASSET ERROR: nonempty m_conditionsYesVec.size(), see srj"));
			info.m_conditionsYesVec.clear();
			info.m_conditionsYesVec.push_back(conditionsYes);
		}
		break;
	}

	ini->initFromINI(&info, myFieldParse);

	if (info.m_modelName.isEmpty())
	{
		DEBUG_CRASH(("*** ASSET ERROR: you must specify a model name"));
		throw INI_INVALID_DATA;
	}
	else if (info.m_modelName.isNone())
	{
		info.m_modelName.clear();
	}

	if ((info.m_iniReadFlags & (1<<GOT_IDLE_ANIMS)) && (info.m_iniReadFlags & (1<<GOT_NONIDLE_ANIMS)))
	{
		DEBUG_CRASH(("*** ASSET ERROR: you should not specify both Animations and IdleAnimations for the same state"));
		throw INI_INVALID_DATA;
	}

	if ((info.m_iniReadFlags & (1<<GOT_IDLE_ANIMS)) && (info.m_mode != RenderObjClass::ANIM_MODE_ONCE && info.m_mode != RenderObjClass::ANIM_MODE_ONCE_BACKWARDS))
	{
		DEBUG_CRASH(("*** ASSET ERROR: Idle Anims should always use ONCE or ONCE_BACKWARDS (%s)\n",TheThingTemplateBeingParsedName.str()));
		throw INI_INVALID_DATA;
	}

	info.m_validStuff &= ~ModelConditionInfo::HAS_PROJECTILE_BONES;
	for (int wslot = 0; wslot < WEAPONSLOT_COUNT; ++wslot)
	{
		if (info.m_weaponProjectileLaunchBoneName[wslot].isNotEmpty())
		{
			info.m_validStuff |= ModelConditionInfo::HAS_PROJECTILE_BONES;
			break;
		}
	}

	if (cst == ParseCondStateType::PARSE_TRANSITION)
	{
		if (info.m_iniReadFlags & (1<<GOT_IDLE_ANIMS))
		{
			DEBUG_CRASH(("*** ASSET ERROR: Transition States should not specify Idle anims"));
			throw INI_INVALID_DATA;
		}

		if (info.m_mode != RenderObjClass::ANIM_MODE_ONCE && info.m_mode != RenderObjClass::ANIM_MODE_ONCE_BACKWARDS)
		{
			DEBUG_CRASH(("*** ASSET ERROR: Transition States should always use ONCE or ONCE_BACKWARDS"));
			throw INI_INVALID_DATA;
		}

		if (info.m_transitionKey != NAMEKEY_INVALID || info.m_allowToFinishKey != NAMEKEY_INVALID)
		{
			DEBUG_CRASH(("*** ASSET ERROR: Transition States must not have transition keys or m_allowToFinishKey"));
			throw INI_INVALID_DATA;
		}

		self->m_transitionMap[info.m_transitionSig] = info;
	}
	else
	{
		self->m_conditionStates.push_back(info);
	}
}

//-------------------------------------------------------------------------------------------------
const ModelConditionInfo* W3DModelDrawModuleData::findBestInfo(const ModelConditionFlags& c) const
{
	ModelConditionFlags bits = c;
	bits.clear(m_ignoreConditionStates);
	return m_conditionStateMap.findBestInfo(m_conditionStates, bits);
}
