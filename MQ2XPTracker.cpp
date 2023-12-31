// MQ2XPTracker.cpp : A (hopefully) simple XP tracker (by Cr4zyb4rd)
//
// Loosely based on XPTracker.mac by Kambic
//
// Usage: /xptracker       - Display time tracking was started.
//        /xptracker reset - Reset all events and begin tracking as if plugin was just loaded
//        /xptracker total - Display total gains since tracking start
//        /xptracker quiet - Toggle output of tracking messages
//        /xpevents        - list the events/timestamps of the events we've tracked
//        /xpevents [#]    - lists the events tracked in the past [#] seconds
//        /xpaverage       - lists the average (mean) xp per-kill
//
// MQ2Data:
//
// xptracker XPTracker[xp|aa|laa|rlaa]
//
// members:
// float    Total         Total % gained since tracking started
// float    Average       Average gain per-change/kill (Everquest "points" format)
// float    AveragePct    Average gain per-change/kill as a %
// float    TimeToDing    Estimated hours until ding
//
// with no index:
// string   RunTime       Time since tracking started in HH:MM:SS format
// float    RunTimeHours  Time since tracking started in hours
// float    KillsPerHour  Estimated number of changes per hour
// float    Changes       Total number of changes tracked so far
//
// Changes:
//  10-20-19
//		Updated to remove LossFromDeath if statement and variables. Modified the formula to account for loss of level. When doing a += value, if the value is negative,
//			it actually subtracts the result. Allowing us to do the same math for loss from death and gain from kill. However, the formula required tweaking in the event
//			you lost a level from death.
//		Updated to add some additional color while I was in here to the /xp command for the averages and time to ding etc output.
//		Updated to change variable types to match each other so as to cutback on the multiple types of variables doing math with each other to reduce the chance of issues
//			in calculations.
//		Update to fix "/xp reset" not resetting totals.
//  07-03-19
//		Update to reflect gaining multiple AA's per kill.
//		Update to correct the AA Exp formula.
//		Update to fix indentation.
//		Update to add color to XP and AAXP output after each kill.
//		Update to change output format of XP and AAXP for each kill to show the AA earned as a number of actual AAs.
//  06-22-19
//		Updated to follow new XP formula while preserving previous formula for RoF2EMU and UFEMU
//
//  02-20-14
//     Removed all leadership AA tracking. - dewey2461
//
//  10-31-04
//     Finished off(??) the MQ2Data, bumping the version number to 2.0
//
//  10-29-04
//     Finally added time-to-ding to /xpaverage.
//
//  10-23-04
//     Added a small fix to the "reset" option as a possible workaround for those with no HUD.
//     Disabled INI writing until something's actually done with the data.
//
//  09-21-04
//     Made XP check vs a local variable instead of having an extra DWORD in the struct
//
//  09-19-04
//     New changelog. Less clutter.  Meh.
//     Everything's done with the points straight from the client.  1-330 for xp/aa
//     and 1-330 for laa/rlaa.  Percentages are only displayed when needed in output.
//     No sense dealing with the issues in storing a float, and the possible rounding
//     errors etc.
//////////////////////////////////////////////////////////////////////////////

// FIXME:  Lots of narrowing conversions.  The methodology should probably just be rewritten.

#include <mq/Plugin.h>
//////////////////////////////////////////////////////////////////////////////
// Change this if the plugin runs too slow or too fast.  It simply specifies
// how many MQ2 "pulses" to skip between experience checks.
constexpr auto SKIP_PULSES = 3;
//////////////////////////////////////////////////////////////////////////////
constexpr auto SECOND = 1000;
constexpr auto MINUTE = (60 * SECOND);
constexpr auto HOUR = (60 * MINUTE);
constexpr auto DAY = (24 * HOUR);

#include <list>

PreSetup("MQ2XPTracker");
PLUGIN_VERSION(2.1);

DWORD GetTotalAA();

#if defined(UFEMU) || defined(ROF2EMU)
	int64_t XPTotalPerLevel = 330;
	float XPTotalDivider = 3.30f;
#else
	int64_t XPTotalPerLevel = 100000;
	float XPTotalDivider = 1000.0f;
#endif

enum XP_TYPES {
	Experience,
	AltExperience,
};

struct _expdata {
	int64_t Base = 0;
	int64_t Gained = 0;
	int64_t Total = 0;
} TrackXP[4];

typedef struct _timestamp {
	SYSTEMTIME systime;
	int64_t  systicks;
} TIMESTAMP;

struct _XP_EVENT {
	int64_t   xp;
	int64_t   aa;
	TIMESTAMP   Timestamp;
};

bool bTrackXP = false;
bool bDoInit = false;
bool bQuietXP = false;
bool bFirstCall = true;
bool bResetOnZone = true;
int PlayerLevel = 0;
DWORD PlayerAA = 0;
TIMESTAMP StartTime;
std::list<_XP_EVENT> Events;
std::list<_XP_EVENT>::iterator pEvents;

struct AverageInfo {
	float xp = 0.f;
	float aa = 0.f;
};

AverageInfo GetAverages()
{
	if (Events.empty()) return AverageInfo{};
	int64_t xp = 0;
	int64_t aa = 0;
	pEvents = Events.begin();
	int i = 0;
	while (pEvents != Events.end()) {
		xp += pEvents->xp;
		aa += pEvents->aa;
		++i;
		++pEvents;
	}

	AverageInfo Averages;
	Averages.xp = static_cast<float>(xp) / i;
	Averages.aa = static_cast<float>(aa) / i;
	return Averages;
}

float GetKPH()
{
	int Kills = (int)Events.size();
	uint64_t RunningTime = GetTickCount64() - StartTime.systicks;
	float RunningTimeFloat = (float)RunningTime / HOUR;
	return Events.empty() ? 0 : Kills / RunningTimeFloat;
}

enum class XPTrackerID {
	Total = 0,
	XP = 1,
	AA = 2,
};

float GetEPH(XPTrackerID Type)
{
	uint64_t RunningTime = GetTickCount64() - StartTime.systicks;
	float RunningTimeFloat = (float)RunningTime / HOUR;

	if (Type == XPTrackerID::XP)
	{
		float TotalXP = (float)TrackXP[Experience].Total / XPTotalDivider;
		return TotalXP / RunningTimeFloat;
	}

	if (Type == XPTrackerID::AA)
	{
		float TotalXP = (float)TrackXP[AltExperience].Total / XPTotalDivider;
		return TotalXP / RunningTimeFloat;
	}

	float TotalXP = (float)TrackXP[Experience].Total / XPTotalDivider + (float)TrackXP[AltExperience].Total / XPTotalDivider;
	return TotalXP / RunningTimeFloat;
}

char* GetRunTime(char* szTemp, size_t bufferSize)
{
	uint64_t RunningTime = GetTickCount64() - StartTime.systicks;
	uint64_t RunningTimeHours = RunningTime / HOUR;
	uint64_t RunningTimeMinutes = (RunningTime - (RunningTimeHours * HOUR)) / MINUTE;
	uint64_t RunningTimeSeconds = (RunningTime - (RunningTimeHours * HOUR + RunningTimeMinutes * MINUTE)) / SECOND;
	sprintf_s(szTemp, bufferSize, "%02lld:%02lld:%02lld", RunningTimeHours, RunningTimeMinutes, RunningTimeSeconds);
	return szTemp;
}

class MQ2XPTrackerType : public MQ2Type
{
public:
	enum class XPTrackerMembers
	{
		Total = 1,
		Average,
		AveragePct,
		TimeToDing,
		KillsPerHour,
		Changes,
		RunTime,
		RunTimeHours,
		PctExpPerHour,
	};

	MQ2XPTrackerType() : MQ2Type("xptracker")
	{
		ScopedTypeMember(XPTrackerMembers, Total);
		ScopedTypeMember(XPTrackerMembers, Average);
		ScopedTypeMember(XPTrackerMembers, AveragePct);
		ScopedTypeMember(XPTrackerMembers, TimeToDing);
		ScopedTypeMember(XPTrackerMembers, KillsPerHour);
		ScopedTypeMember(XPTrackerMembers, Changes);
		ScopedTypeMember(XPTrackerMembers, RunTime);
		ScopedTypeMember(XPTrackerMembers, RunTimeHours);
		ScopedTypeMember(XPTrackerMembers, PctExpPerHour);
	}

	virtual bool GetMember(MQVarPtr VarPtr, const char* Member, char* Index, MQTypeVar& Dest) override
	{
		auto pMember = MQ2XPTrackerType::FindMember(Member);
		if (!pMember)
			return false;

		XPTrackerID id = static_cast<XPTrackerID>(VarPtr.DWord);
		switch (static_cast<XPTrackerMembers>(pMember->ID))
		{
		case XPTrackerMembers::Total:
			switch (id)
			{
			case XPTrackerID::Total:
				Dest.Float = (float)TrackXP[Experience].Total / XPTotalDivider + (float)TrackXP[AltExperience].Total / XPTotalDivider;
				break;
			case XPTrackerID::XP:
				Dest.Float = (float)TrackXP[Experience].Total / XPTotalDivider;
				break;
			case XPTrackerID::AA:
				Dest.Float = (float)TrackXP[AltExperience].Total / XPTotalDivider;
				break;
			default:
				return false;
			}
			Dest.Type = mq::datatypes::pFloatType;
			return true;

		case XPTrackerMembers::Average: {
			if (id == XPTrackerID::Total)
				return false;

			switch (id)
			{
			case XPTrackerID::XP:
				Dest.Float = GetAverages().xp;
				break;
			case XPTrackerID::AA:
				Dest.Float = GetAverages().aa;
				break;
			default:
				return false;
			}
			Dest.Type = mq::datatypes::pFloatType;
			return true;
		}

		case XPTrackerMembers::AveragePct:
			switch (id)
			{
			case XPTrackerID::XP:
				Dest.Float = GetAverages().xp / XPTotalDivider;
				break;
			case XPTrackerID::AA:
				Dest.Float = GetAverages().aa / XPTotalDivider;
				break;
			default:
				return false;
			}
			Dest.Type = mq::datatypes::pFloatType;
			return true;

		case XPTrackerMembers::TimeToDing:
			if (!pCharData) return false;
			switch (id)
			{
			case XPTrackerID::XP:
				Dest.Float = static_cast<float>(XPTotalPerLevel - pCharData->Exp) / (GetAverages().xp * GetKPH());
				break;
			case XPTrackerID::AA:
				Dest.Float = static_cast<float>(XPTotalPerLevel - pCharData->AAExp) / (GetAverages().aa * GetKPH());
				break;
			default:
				return false;
			}
			Dest.Type = mq::datatypes::pFloatType;
			return true;

		case XPTrackerMembers::KillsPerHour:
			if (id != XPTrackerID::Total) return false;
			Dest.Float = GetKPH();
			Dest.Type = mq::datatypes::pFloatType;
			return true;

		case XPTrackerMembers::Changes:
			if (id != XPTrackerID::Total) return false;
			Dest.Int = (int)Events.size();
			Dest.Type = mq::datatypes::pIntType;
			return true;

		case XPTrackerMembers::RunTime:
			if (id != XPTrackerID::Total) return false;
			Dest.Ptr = GetRunTime(DataTypeTemp, DataTypeTemp.size());
			Dest.Type = mq::datatypes::pStringType;
			return true;

		case XPTrackerMembers::RunTimeHours:
			if (id != XPTrackerID::Total) return false;
			Dest.Float = ((float)(GetTickCount64() - StartTime.systicks) / HOUR);
			Dest.Type = mq::datatypes::pFloatType;
			return true;

		case XPTrackerMembers::PctExpPerHour:
			Dest.Float = GetEPH(id);
			Dest.Type = mq::datatypes::pFloatType;
			return true;
		}
		return false;
	}

	bool ToString(MQVarPtr VarPtr, char* Destination) override
	{
		if (bTrackXP)
			strcpy_s(Destination, MAX_STRING, "TRUE");
		else
			strcpy_s(Destination, MAX_STRING, "FALSE");

		return true;
	}
};
MQ2XPTrackerType* pXPTrackerType =  nullptr;


bool dataXPTracker(const char* szIndex, MQTypeVar& Ret)
{
	XPTrackerID id = XPTrackerID::Total;

	if (szIndex[0])
	{
		if (IsNumber(szIndex))
		{
			id = static_cast<XPTrackerID>(atoi(szIndex));
			if (id != XPTrackerID::Total
				&& id != XPTrackerID::XP
				&& id != XPTrackerID::AA)
			{
				return false;
			}
		}
		else if (!_stricmp(szIndex, "xp"))
		{
			id = XPTrackerID::XP;
		}
		else if (!_stricmp(szIndex, "aa"))
		{
			id = XPTrackerID::AA;
		}
		else
		{
			return false;
		}
	}

	Ret.DWord = static_cast<uint32_t>(id);
	Ret.Type = pXPTrackerType;
	return true;
}

VOID AddElement(int64_t Experience, int64_t AA)
{
	_XP_EVENT event;
	event.xp=Experience;
	event.aa=AA;
	event.Timestamp.systicks=GetTickCount64();
	::GetLocalTime(&event.Timestamp.systime);
	Events.push_back(event);
  /*   pEvents=Events.end();
  pEvents--;
  char szTemp1[MAX_STRING];
  char szTemp2[MAX_STRING];
  sprintf_s(szTemp1,"XPTrack%d",Events.size()+1);
  sprintf_s(szTemp2,"%d,%d,%d,%d,%d,%d,%d,%d,%d",
  pEvents->xp,
  pEvents->aa,
  pEvents->laa,
  pEvents->rlaa,
  pEvents->Timestamp.systime.wMonth,
  pEvents->Timestamp.systime.wDay,
  pEvents->Timestamp.systime.wHour,
  pEvents->Timestamp.systime.wMinute,
  pEvents->Timestamp.systime.wSecond );
  WritePrivateProfileString("MQ2XPTracker",szTemp1,szTemp2,INIFileName);
  */
}

BOOL CheckExpChange()
{
	PCHARINFO pCharInfo = GetCharInfo();
	PcProfile* pCharInfo2 = GetPcProfile();
	int64_t Current = pCharInfo->Exp;
	if (Current!=TrackXP[Experience].Base) {
		TrackXP[Experience].Gained = (pCharInfo2->Level == PlayerLevel ? Current - TrackXP[Experience].Base : (pCharInfo2->Level > PlayerLevel ? XPTotalPerLevel - TrackXP[Experience].Base + Current : TrackXP[Experience].Base - XPTotalPerLevel + Current));
		TrackXP[Experience].Total += TrackXP[Experience].Gained;
		TrackXP[Experience].Base = Current;
		PlayerLevel = pCharInfo2->Level;
		return true;
	}
	TrackXP[Experience].Gained=0;
	return false;
}

BOOL CheckAAChange()
{
	PCHARINFO pCharInfo = GetCharInfo();
	PcProfile* pCharInfo2 = GetPcProfile();
	DWORD Current = pCharInfo->AAExp;
	if (Current!=TrackXP[AltExperience].Base) {
		TrackXP[AltExperience].Gained = GetTotalAA() == PlayerAA ? Current - TrackXP[AltExperience].Base : Current - TrackXP[AltExperience].Base + ((GetTotalAA() - PlayerAA) * XPTotalPerLevel);
		TrackXP[AltExperience].Total +=TrackXP[AltExperience].Gained;
		TrackXP[AltExperience].Base = Current;
		PlayerAA = GetTotalAA();
		return true;
	}
	return false;
}

VOID SetBaseValues()
{
	PCHARINFO pCharInfo = GetCharInfo();
	PcProfile* pCharInfo2 = GetPcProfile();
	TrackXP[Experience].Base = pCharInfo->Exp;
	TrackXP[AltExperience].Base = pCharInfo->AAExp;

	if (bResetOnZone || bFirstCall) {
		TrackXP[Experience].Total = 0;
		TrackXP[AltExperience].Total = 0;
	}

	PlayerLevel = pCharInfo2->Level;
	PlayerAA = GetTotalAA();
}

DWORD GetTotalAA()
{
	return GetPcProfile()->AAPoints + GetPcProfile()->AAPointsSpent;
}

VOID XPEventsCommand(PSPAWNINFO pChar, PCHAR szLine)
{
	char szTemp[MAX_STRING];
	int64_t TargetTick;
	GetArg(szTemp,szLine,1);
	if (!strlen(szTemp)) TargetTick=GetTickCount64()-HOUR;
	else {
		if (!IsNumber(szTemp)) {
			if (!_strnicmp(szTemp,"hour",4))
			{
				TargetTick=GetTickCount64()-HOUR;
			}
			else
			{
				WriteChatColor("/xpevents requires a numeric argument in seconds",CONCOLOR_RED);
				return;
			}
		}
		else
		{
			TargetTick=GetTickCount64()-(SECOND*GetIntFromString(szTemp, 0));
		}
	}

	if (Events.empty()) {
		WriteChatColor("MQ2XPTracker::No experience changes tracked",USERCOLOR_DEFAULT);
		return;
	}
	WriteChatf("%d experiences changes tracked:",Events.size());
	pEvents = Events.begin();
	int i=1;
	while (pEvents!=Events.end()) {
		if (pEvents->Timestamp.systicks>TargetTick) {
			sprintf_s(szTemp,"%02d:%02d:%02d",pEvents->Timestamp.systime.wHour,pEvents->Timestamp.systime.wMinute,pEvents->Timestamp.systime.wSecond);
			WriteChatf("%03d - %02.2f%%XP %02.2f%%AA %02.2f%% at %s (%lld system ticks):",
			i,
			(float)pEvents->xp/XPTotalDivider,
			(float)pEvents->aa/XPTotalDivider,
			szTemp,
			pEvents->Timestamp.systicks);

		}
		pEvents++;
		i++;
	}
}


VOID XPTrackerCommand(PSPAWNINFO pChar, PCHAR szLine)
{
	char szTemp[MAX_STRING];
	GetArg(szTemp,szLine,1);
	if (!_strnicmp(szTemp, "resetonzone", 12)) {
		bResetOnZone = !bResetOnZone;
		WritePrivateProfileBool("General", "ResetOnZone", bResetOnZone, INIFileName);
		WriteChatf("MQ2XPTracker::Reset XP Tracking when zoning is now %s", (bResetOnZone ? "true" : "false"));
		return;
	} else if (!_strnicmp(szTemp,"reset",5)) {
		bDoInit=true;
		bFirstCall=true;
		WriteChatColor("MQ2XPTracker::XP tracking reset.");
	} else if (!_strnicmp(szTemp,"total",5)) {
		sprintf_s(szTemp,"Total XP Gained (Normal/AA): %03.3f%%/%03.3f%%",(float)TrackXP[Experience].Total/XPTotalDivider,(float)TrackXP[AltExperience].Total/XPTotalDivider);
		WriteChatColor(szTemp);
		return;
	} else if (!_strnicmp(szTemp,"quiet",5)) {
		bQuietXP = !bQuietXP;
		WriteChatf("MQ2XPTracker::Quiet mode %s", (bQuietXP ? "\agTrue" : "\arFalse"));
		WritePrivateProfileBool("General", "Quiet", bQuietXP, INIFileName);
		return;
	}

	if (bDoInit) {
		SetBaseValues();
		if (bFirstCall) {
			Events.clear();
			::GetLocalTime(&StartTime.systime);
			StartTime.systicks=GetTickCount64();
			bFirstCall = false;
		}
		bDoInit = false;
		bTrackXP = true;
	}
	WriteChatf("MQ2XPTracker::XP tracking started at %02d:%02d:%02d (%lld system ticks)",StartTime.systime.wHour,StartTime.systime.wMinute,StartTime.systime.wSecond,StartTime.systicks);
}

VOID XPAverageCommand(PSPAWNINFO pChar, PCHAR szLine)
{
	float xp=0;
	float aa=0;

	char szTemp[MAX_STRING];
	GetArg(szTemp,szLine,1);
	if (!_strnicmp(szTemp,"reset",5)) {
		XPTrackerCommand(pChar,szLine);
	}

	if (Events.empty()) {
		WriteChatColor("MQ2XPTracker::No experience changes tracked",USERCOLOR_DEFAULT);
		return;
	}

	pEvents = Events.begin();
	int i=0;
	while (pEvents!=Events.end()) {
		xp+=pEvents->xp;
		aa+=pEvents->aa;
		i++;
		pEvents++;
	}
	int64_t RunningTime = GetTickCount64() - StartTime.systicks;
	int64_t RunningTimeHours = RunningTime/HOUR;
	int64_t RunningTimeMinutes = (RunningTime-(RunningTimeHours*HOUR))/MINUTE;
	int64_t RunningTimeSeconds = (RunningTime-(RunningTimeHours*HOUR+RunningTimeMinutes*MINUTE))/SECOND;
	float RunningTimeFloat = (float)RunningTime/HOUR;
	float perkill;
	float perhour;
	int64_t needed;
	float KPH = (float)i/RunningTimeFloat;
	WriteChatf("\a-tTotal run time: \ag%lld \a-thours \ag%lld \a-tminutes \ag%lld \a-tseconds\ax", RunningTimeHours, RunningTimeMinutes, RunningTimeSeconds);
	WriteChatf("\a-tAverage \atEXP \a-tper kill: \ag%02.3f%% \a-tper-hour: \ag%02.1f%%\ax",(float)(xp/XPTotalDivider)/i,(float)(xp/XPTotalDivider)/i*KPH);
	WriteChatf("\a-tAverage \atAAEXP \a-tper kill: \ag%02.3f%% \a-tper-hour: \ag%02.1f%%\ax",(float)(aa/XPTotalDivider)/i,(float)(aa/XPTotalDivider)/i*KPH);
	WriteChatf("\a-tAverage \ag%1.2f \a-tkills-per-hour", KPH);

	if (xp)
	{
		needed = XPTotalPerLevel-GetCharInfo()->Exp;
		perkill = xp/i;
		perhour = perkill*KPH;
		WriteChatf("\ayEstimated time to \atLevel \ag%1.2f \ayhours", (float)needed/perhour);
	}

	if (aa)
	{
		needed = XPTotalPerLevel-GetCharInfo()->AAExp;
		perkill = aa/i;
		perhour = perkill*KPH;
		WriteChatf("\ayEstimated time to \atAA \ayding \ag%1.2f \ayhours", (float)needed/perhour);
	}
}


// Called once, when the plugin is to initialize
PLUGIN_API VOID InitializePlugin()
{
	DebugSpewAlways("Initializing MQ2XPTracker");

	AddCommand("/xpevents",XPEventsCommand);
	AddCommand("/xptracker",XPTrackerCommand);
	AddCommand("/xpaverage",XPAverageCommand);
	AddMQ2Data("XPTracker",dataXPTracker);

	pXPTrackerType = new MQ2XPTrackerType;

	//Load any stored options. Not character/server specific so can do this in the Init without crashing.
	bResetOnZone = GetPrivateProfileBool("General", "ResetOnZone", true, INIFileName);
	bQuietXP = GetPrivateProfileBool("General", "Quiet", false, INIFileName);
}

// Called once, when the plugin is to shutdown
PLUGIN_API VOID ShutdownPlugin()
{
	DebugSpewAlways("Shutting down MQ2XPTracker");
	Events.clear();
	RemoveCommand("/xpevents");
	RemoveCommand("/xptracker");
	RemoveCommand("/xpaverage");
	RemoveMQ2Data("XPTracker");

	delete pXPTrackerType;
}

PLUGIN_API void SetGameState(DWORD GameState)
{
	DebugSpewAlways("MQ2XPTracker::SetGameState()");
	if (GameState!=GAMESTATE_INGAME)
	{
		bTrackXP = false; // don't track while not in game
	}
	else
	{
		bDoInit = true;
	}
}

PLUGIN_API VOID OnDrawHUD()
{
	if (bDoInit) {//TODO: This doesn't belong here??
		SetBaseValues();
		if (bFirstCall) {
			Events.clear();
			::GetLocalTime(&StartTime.systime);
			StartTime.systicks = GetTickCount64();
			bFirstCall = false;
		}
		bDoInit = false;
		bTrackXP = true;
	}
}

PLUGIN_API VOID OnPulse()
{
	static int N=0;
	bool gainedxp;
	char szTemp[MAX_STRING];

	if ((!bTrackXP || gGameState != GAMESTATE_INGAME) || ++N<=SKIP_PULSES) return;
	N=0;
	gainedxp=false;
	if (CheckExpChange()) {
		gainedxp = true;
		if (!bQuietXP){
			sprintf_s(szTemp,"\ayXP Gained: \ag%02.3f%% \aw|| \ayXP Total: \ag%02.3f%%", (float)TrackXP[Experience].Gained/XPTotalDivider, TrackXP[Experience].Total/XPTotalDivider);
			WriteChatColor(szTemp);
		}
	}
	if (GetCharInfo()->PercentEXPtoAA && CheckAAChange()) {
		gainedxp = true;
		if (!bQuietXP){
			#if defined(UFEMU) || defined(ROF2EMU)
				sprintf_s(szTemp, "\ayAA Gained: \ag%2.2f \aw(\at%02.3f%%\aw) \aw|| \ayAA Total: \ag%2.2f \aw(\at%02.3f%%\aw)", (float)TrackXP[AltExperience].Gained / 330.0f, (float)TrackXP[AltExperience].Gained / XPTotalDivider, (float)TrackXP[AltExperience].Total / 330.0f, (float)TrackXP[AltExperience].Total / XPTotalDivider);
			#else
				sprintf_s(szTemp,"\ayAA Gained: \ag%2.2f \aw(\at%02.3f%%\aw) \aw|| \ayAA Total: \ag%2.2f \aw(\at%02.3f%%\aw)", (float)TrackXP[AltExperience].Gained / 100000.0f,(float)TrackXP[AltExperience].Gained/XPTotalDivider, (float)TrackXP[AltExperience].Total / 100000.0f, (float)TrackXP[AltExperience].Total/XPTotalDivider);
				WriteChatColor(szTemp);
			#endif
		}
	}
	if (gainedxp)
	{
		AddElement(TrackXP[Experience].Gained,TrackXP[AltExperience].Gained);
	}
}
