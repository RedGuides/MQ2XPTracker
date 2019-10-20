// MQ2XPTracker.cpp : A (hopefully) simple XP tracker (by Cr4zyb4rd)
//
// Version 2.1
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
//		Potentially TODO: Update GetTickCount() to use GetTickCount64() as recommended by VS. 
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

#include "../MQ2Plugin.h"
using namespace std;
//////////////////////////////////////////////////////////////////////////////
// Change this if the plugin runs too slow or too fast.  It simply specifies
// how many MQ2 "pulses" to skip between experience checks.
#define SKIP_PULSES 3
//////////////////////////////////////////////////////////////////////////////
#define SECOND 1000
#define MINUTE (60 * SECOND)
#define HOUR (60 * MINUTE)
#define DAY (24 * HOUR)

#include <list>

PreSetup("MQ2XPTracker");

DWORD GetTotalAA();

#if defined(UFEMU) || defined(ROF2EMU)
	long long XPTotalPerLevel = 330;
	float XPTotalDivider = 3.30f;
#else
	long long XPTotalPerLevel = 100000;
	float XPTotalDivider = 1000.0f;
#endif

enum XP_TYPES {
	Experience,
	AltExperience,
};

struct _expdata {
	long long Base;
	long long Gained;
	long long Total;
} TrackXP[4];

typedef struct _timestamp {
	SYSTEMTIME systime;
	DWORD      systicks;
} TIMESTAMP;

struct _XP_EVENT {
	long long xp;
	long long aa;
	TIMESTAMP Timestamp;
};

bool bTrackXP = false;
bool bDoInit = false;
bool bQuietXP = false;
bool bFirstCall = true;
DWORD PlayerLevel = 0;
DWORD PlayerAA = 0;
TIMESTAMP StartTime;
list<_XP_EVENT> Events;
list<_XP_EVENT>::iterator pEvents;

class MQ2XPTrackerType *pXPTrackerType=0;

class MQ2XPTrackerType : public MQ2Type
{
	private:
		int _id;
		struct {
			FLOAT xp;
			FLOAT aa;
		} Averages;
	public:
	enum XPTrackerMembers
	{
		Total=1,
		Average=2,
		AveragePct=3,
		TimeToDing=4,
		KillsPerHour=5,
		Changes=6,
		RunTime=7,
		RunTimeHours=8,
		PctExpPerHour=9,
	};
	MQ2XPTrackerType():MQ2Type("xptracker")
	{
		TypeMember(Total);
		TypeMember(Average);
		TypeMember(AveragePct);
		TypeMember(TimeToDing);
		TypeMember(KillsPerHour);
		TypeMember(Changes);
		TypeMember(RunTime);
		TypeMember(RunTimeHours);
		TypeMember(PctExpPerHour);
	}
	~MQ2XPTrackerType()
	{
	}
	void SetIndex(int id)
	{
		_id = id;
	}
	void GetAverages()
	{
		Averages.xp = 0;
		Averages.aa = 0;
		if (Events.empty()) return;
		pEvents = Events.begin();
		int i=0;
		while (pEvents!=Events.end()) {
			Averages.xp+=pEvents->xp;
			Averages.aa+=pEvents->aa;
			i++;
			pEvents++;
		}
		Averages.xp=Averages.xp/i;
		Averages.aa=Averages.aa/i;
	}

	FLOAT GetKPH()
	{
		DWORD Kills = Events.size();
		DWORD RunningTime = GetTickCount() - StartTime.systicks;
		FLOAT RunningTimeFloat = (float)RunningTime/HOUR;
		return Events.empty()?0:(float)Kills/RunningTimeFloat;
	}

	FLOAT GetEPH(PCHAR Type)
	{
		DWORD RunningTime = GetTickCount() - StartTime.systicks;
		FLOAT RunningTimeFloat = (float)RunningTime/HOUR;

		if(!strcmp(Type,"Experience"))
		{
			FLOAT TotalXP = (float)TrackXP[Experience].Total/XPTotalDivider;
			return (float)TotalXP/RunningTimeFloat;
		}
		else if(!strcmp(Type,"AltExperience"))
		{
			FLOAT TotalXP = (float)TrackXP[AltExperience].Total/XPTotalDivider;
			return (float)TotalXP/RunningTimeFloat;
		}

		FLOAT TotalXP = (float)TrackXP[Experience].Total/XPTotalDivider + (float)TrackXP[AltExperience].Total/XPTotalDivider ;
		return (float)TotalXP/RunningTimeFloat;

	}

	PCHAR GetRunTime(PCHAR szTemp)
	{
		DWORD RunningTime = GetTickCount() - StartTime.systicks;
		DWORD RunningTimeHours = RunningTime/HOUR;
		DWORD RunningTimeMinutes = (RunningTime-(RunningTimeHours*HOUR))/MINUTE;
		DWORD RunningTimeSeconds = (RunningTime-(RunningTimeHours*HOUR+RunningTimeMinutes*MINUTE))/SECOND;
		sprintf_s(szTemp,MAX_STRING,"%02d:%02d:%02d",RunningTimeHours,RunningTimeMinutes,RunningTimeSeconds);
		return szTemp;
	}

	bool GetMember(MQ2VARPTR VarPtr, PCHAR Member, PCHAR Index, MQ2TYPEVAR &Dest)
	{
		PMQ2TYPEMEMBER pMember=MQ2XPTrackerType::FindMember(Member);
		if (!pMember)
			return false;
		switch((XPTrackerMembers)pMember->ID)
		{
			case Total:
				switch (_id)
				{
					case 0:
						Dest.Float=(float)TrackXP[Experience].Total/XPTotalDivider + (float)TrackXP[AltExperience].Total/XPTotalDivider ;
						break;
					case 1:
						Dest.Float=(float)TrackXP[Experience].Total/XPTotalDivider;
						break;
					case 2:
						Dest.Float=(float)TrackXP[AltExperience].Total/XPTotalDivider;
						break;
					default:
						return false;
				}
				Dest.Type=pFloatType;
				return true;
			case Average:
				GetAverages();
				switch (_id)
				{
					case 0:
						return false;
					case 1:
						Dest.Float=Averages.xp;
						break;
					case 2:
						Dest.Float=Averages.aa;
						break;
					default:
						return false;
				}
				Dest.Type=pFloatType;
				return true;
			case AveragePct:
				GetAverages();
				switch (_id)
				{
					case 1:
						Dest.Float=Averages.xp/XPTotalDivider;
						break;
					case 2:
						Dest.Float=Averages.aa/XPTotalDivider;
						break;
					default:
						return false;
				}
				Dest.Type=pFloatType;
				return true;
			case TimeToDing:
				__int64  needed;
				GetAverages();
				switch (_id)
				{
					case 1:
						needed = XPTotalPerLevel-GetCharInfo()->Exp;
						Dest.Float=(float)needed/(Averages.xp*GetKPH());
						break;
					case 2:
						needed = XPTotalPerLevel-GetCharInfo()->AAExp;
						Dest.Float=(float)needed/(Averages.aa*GetKPH());
						break;
					default:
						return false;
				}
				Dest.Type=pFloatType;
				return true;
			case KillsPerHour:
				if (_id) return false;
				Dest.Float=GetKPH();
				Dest.Type=pFloatType;
				return true;
			case Changes:
				if (_id) return false;
				Dest.Int=Events.size();
				Dest.Type=pIntType;
				return true;
			case RunTime:
				if (_id) return false;
				Dest.Ptr=GetRunTime(DataTypeTemp);
				Dest.Type=pStringType;
				return true;
			case RunTimeHours:
				if (_id) return false;
				Dest.Float=(float)((GetTickCount() - StartTime.systicks)/HOUR);
				Dest.Type=pFloatType;
				return true;
			case PctExpPerHour:
				switch (_id)
				{
					case 0:
						Dest.Float=GetEPH("Overall");
						break;
					case 1:
						Dest.Float=GetEPH("Experience");
						break;
					case 2:
						Dest.Float=GetEPH("AltExperience");
						break;
					default:
						return false;
				}
				Dest.Type=pFloatType;
				return true;
		}
		return false;
	}

	bool ToString(MQ2VARPTR VarPtr, PCHAR Destination)
	{
		if (bTrackXP)
			strcpy_s(Destination,MAX_STRING,"TRUE");
		else
			strcpy_s(Destination,MAX_STRING,"FALSE");
		return true;
	}

	bool FromData(MQ2VARPTR &VarPtr, MQ2TYPEVAR &Source)
	{
		return false;
	}

	bool FromString(MQ2VARPTR &VarPtr, PCHAR Source)
	{
		return false;
	}
};

BOOL dataXPTracker(PCHAR szIndex, MQ2TYPEVAR &Ret)
{
	int id;
	if (!szIndex[0])
	{
		id = 0;
	}
	else if (IsNumber(szIndex))
	{
		id = atoi(szIndex);
	}
	else if (!_stricmp(szIndex,"xp"))
	{
		id = 1;
	}
	else if (!_stricmp(szIndex,"aa"))
	{
		id = 2;
	}
	else return false;
	pXPTrackerType->SetIndex(id);
	Ret.DWord=1;
	Ret.Type=pXPTrackerType;
	return true;
}

VOID AddElement(__int64 Experience, __int64 AA)
{
	_XP_EVENT event;
	event.xp=Experience;
	event.aa=AA;
	event.Timestamp.systicks=GetTickCount();
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
	PCHARINFO2 pCharInfo2 = GetCharInfo2();
	long long Current = pCharInfo->Exp;
	if (Current!=TrackXP[Experience].Base) {
		TrackXP[Experience].Gained = (pCharInfo2->Level == PlayerLevel ? Current - TrackXP[Experience].Base : (pCharInfo2->Level > PlayerLevel ? XPTotalPerLevel - TrackXP[Experience].Base + Current : TrackXP[Experience].Base - XPTotalPerLevel + Current));
		TrackXP[Experience].Total += TrackXP[Experience].Gained;
		TrackXP[Experience].Base = Current;
		PlayerLevel = pCharInfo2->Level;
		return true;
	}
	return false;
}

BOOL CheckAAChange()
{
	PCHARINFO pCharInfo = GetCharInfo();
	PCHARINFO2 pCharInfo2 = GetCharInfo2();
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
	PCHARINFO2 pCharInfo2 = GetCharInfo2();
	TrackXP[Experience].Base = pCharInfo->Exp;
	TrackXP[Experience].Total = 0;
	TrackXP[AltExperience].Base = pCharInfo->AAExp;
	TrackXP[AltExperience].Total = 0;
	PlayerLevel = pCharInfo2->Level;
	PlayerAA = GetTotalAA();
}

DWORD GetTotalAA()
{
	return GetCharInfo2()->AAPoints + GetCharInfo2()->AAPointsSpent;
}

VOID XPEventsCommand(PSPAWNINFO pChar, PCHAR szLine)
{
	char szTemp[MAX_STRING];
	DWORD TargetTick;
	GetArg(szTemp,szLine,1);
	if (!strlen(szTemp)) TargetTick=GetTickCount()-HOUR;
	else {
		if (!IsNumber(szTemp)) {
			if (!_strnicmp(szTemp,"hour",4)) TargetTick=GetTickCount()-HOUR;
			else {
			WriteChatColor("/xpevents requires a numeric argument in seconds",CONCOLOR_RED);
			return;
			}
		} else TargetTick=GetTickCount()-(atoi(szTemp)*SECOND);
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
			WriteChatf("%03d - %02.2f%%XP %02.2f%%AA %02.2f%% at %s (%d system ticks):",
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
	if (!_strnicmp(szTemp,"reset",5)) {
		bDoInit=true;
		bFirstCall=true;
		WriteChatColor("MQ2XPTracker::XP tracking reset.");
	} else if (!_strnicmp(szTemp,"total",5)) {
		sprintf_s(szTemp,"Total XP Gained (Normal/AA): %03.3f%%/%03.3f%%",(float)TrackXP[Experience].Total/XPTotalDivider,(float)TrackXP[AltExperience].Total/XPTotalDivider);
		WriteChatColor(szTemp);
		return;
	} else if (!_strnicmp(szTemp,"quiet",5)) {
		bQuietXP = !bQuietXP;
		if (bQuietXP) {
			WriteChatColor("MQ2XPTracker::Quiet mode on",USERCOLOR_DEFAULT);
		} else WriteChatColor("MQ2XPTracker::Quiet mode off",USERCOLOR_DEFAULT);
		return;
	}

	if (bDoInit) {
		SetBaseValues();
		if (bFirstCall) {
			Events.clear();
			::GetLocalTime(&StartTime.systime);
			StartTime.systicks=GetTickCount();
			bFirstCall = false;
		}
		bDoInit = false;
		bTrackXP = true;
	}
	WriteChatf("MQ2XPTracker::XP tracking started at %02d:%02d:%02d (%d system ticks)",StartTime.systime.wHour,StartTime.systime.wMinute,StartTime.systime.wSecond,StartTime.systicks);
}

VOID XPAverageCommand(PSPAWNINFO pChar, PCHAR szLine)
{
	float xp=0;
	float aa=0;
	float laa=0;
	float rlaa=0;

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
	DWORD RunningTime = GetTickCount() - StartTime.systicks;
	DWORD RunningTimeHours = RunningTime/HOUR;
	DWORD RunningTimeMinutes = (RunningTime-(RunningTimeHours*HOUR))/MINUTE;
	DWORD RunningTimeSeconds = (RunningTime-(RunningTimeHours*HOUR+RunningTimeMinutes*MINUTE))/SECOND;
	FLOAT RunningTimeFloat = (float)RunningTime/HOUR;
	FLOAT perkill;
	FLOAT perhour;
	__int64 needed;
	FLOAT KPH = (float)i/RunningTimeFloat;
	WriteChatf("\a-tTotal run time: \ag%d \a-thours \ag%d \a-tminutes \ag%d \a-tseconds\ax",RunningTimeHours,RunningTimeMinutes,RunningTimeSeconds);
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
PLUGIN_API VOID InitializePlugin(VOID)
{
	DebugSpewAlways("Initializing MQ2XPTracker");

	AddCommand("/xpevents",XPEventsCommand);
	AddCommand("/xptracker",XPTrackerCommand);
	AddCommand("/xpaverage",XPAverageCommand);
	AddMQ2Data("XPTracker",dataXPTracker);

	pXPTrackerType = new MQ2XPTrackerType;
}

// Called once, when the plugin is to shutdown
PLUGIN_API VOID ShutdownPlugin(VOID)
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
		bTrackXP = false; // don't track while not in game
	else 
		bDoInit = true;
}

PLUGIN_API VOID OnDrawHUD(VOID)
{
	if (bDoInit) {
		SetBaseValues();
		if (bFirstCall) {
			Events.clear();
			::GetLocalTime(&StartTime.systime);
			StartTime.systicks = GetTickCount();
			bFirstCall = false;
		}
		bDoInit = false;
		bTrackXP = true;
	}
}

PLUGIN_API VOID OnPulse(VOID)
{
	static int N=0;
	bool gainedxp;
	char szTemp[MAX_STRING];

	if ((!bTrackXP || MQ2Globals::gGameState != GAMESTATE_INGAME) || ++N<=SKIP_PULSES) return;
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
		AddElement(TrackXP[Experience].Gained, TrackXP[AltExperience].Gained);
	return;
}