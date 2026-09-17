#include <YRPP.h>
#include <Helpers/Macro.h>
#include <TagTypeClass.h>
#include <TriggerTypeClass.h>
#include "command.h"
#include "GeneralUtils.h"
#include "Rules.h"
#include "ToolTipManager.h"
#include <map>
#include <WWMouseClass.h>
#include <cmath> 
#include <string>
#include <cwchar>
#include <cwctype>
#include <algorithm>
#include <variant>

#define COLOR_ALLIEDTT 0xA69F
#define COLOR_SOVIETTT 0xFFE0

bool bObjectInfo = false;
bool bTriggerDebug = false;
bool bPressedInButtonsLayer = false;
bool bTriggerDebugPageEnd = false;
bool bTriggerDebugDetailed = false;
bool bTriggerDebugEdited = false;
bool bTriggerDebugTimerEdited = false;
bool bTriggerExecuteLog = false;

char FinalStringBuffer[0x1000];
wchar_t FinalStringBufferW[0x1000];
wchar_t SearchPattern[0x200] = L"";

#define RECT_COUNT 256
enum CurrentMode : int
{
	ForceRun = 0,
	Enable,
	Disable,
	Destroy,
	ChangeTimer,
	Count
};
enum TriggerSort : int
{
	Raw = 0,
	ByID,
	ByName,
	ByTimeLeft,
	ByLastExecuted,
	ByDestroyed,
	end
};

class TriggerClassExt
{
public:
	int LastExecutedFrame = -1;
	int ExecutedCount = 0;
	std::vector<bool> OccuredEvents;
	bool Destroyed = false;
	int DestroyedFrame = -1;
	TriggerTypeClass* Type = nullptr;
	int ResetTimer = -1;
};

const int TriggerDebugStartX = 10;
const int TriggerDebugStartY = 180;
int TriggerDebugStartIndex = 0;
int HoveredTriggerIndex = -100;
int PageTriggerCount = RECT_COUNT;
int CurrentPage = 0;
CurrentMode Mode = ForceRun;
TriggerSort Sort = Raw;
int ModeIndex = -1;
int ChangedTimer = 0;
RectangleStruct TriggerDebugRect[RECT_COUNT]{0};
RectangleStruct TriggerDebugMode[Count]{0};
RectangleStruct TriggerDebugPageDown{ 0 };
RectangleStruct TriggerDebugPageUp{ 0 };
RectangleStruct TriggerDebugDetailed{ 0 };
RectangleStruct TriggerDebugSort{ 0 };
RectangleStruct TriggerDebugSearch{ 0 };
RectangleStruct TriggerDebugEnableModified{ 0 };

std::vector<TriggerClass*> SortedTriggerArray;
std::vector<TriggerClassExt> DestroyedTriggers;
std::vector<TriggerClassExt> SortedDestroyedTriggers;

std::map<TriggerClass*, TriggerClassExt> TriggerExtMap;

using TriggerVariant = std::variant<TriggerClass**, TriggerClassExt*>;
struct ComparableTrigger {
	TriggerVariant item;

	auto getName() const {
		return std::visit([](auto* obj) {
			if constexpr (std::is_same_v<std::decay_t<decltype(*obj)>, TriggerClass*>) {
				return (*obj)->Type->Name;
			}
			else
				return obj->Type->Name;
			}, item);
	}
	auto getID() const {
		return std::visit([](auto* obj) {
			if constexpr (std::is_same_v<std::decay_t<decltype(*obj)>, TriggerClass*>) {
				return (*obj)->Type->ID;
			}
			else
				return obj->Type->ID;
			}, item);
	}
	auto getTimeLeft() const {
		return std::visit([](auto* obj) {
			if constexpr (std::is_same_v<std::decay_t<decltype(*obj)>, TriggerClass*>) {
				int timeLeft = (*obj)->Enabled ? (*obj)->Timer.GetTimeLeft() : (*obj)->Timer.TimeLeft;
				if (timeLeft <= 0)
					timeLeft = INT_MAX;
				return timeLeft;
			}
			else
				return INT_MAX;
			}, item);
	}
	auto getLastExecuted() const {
		return std::visit([](auto* obj) {
			if constexpr (std::is_same_v<std::decay_t<decltype(*obj)>, TriggerClass*>) {
				return TriggerExtMap[*obj].LastExecutedFrame;
			}
			else
				return obj->LastExecutedFrame;
			}, item);
	}
	auto getDestroyed() const {
		return std::visit([](auto* obj) {
			if constexpr (std::is_same_v<std::decay_t<decltype(*obj)>, TriggerClass*>) {
				return -1;
			}
			else
				return obj->DestroyedFrame;
			}, item);
	}
};

std::vector<ComparableTrigger> SortedAllTriggers;

static std::wstring to_lower(const wchar_t* s) {
	std::wstring result;
	while (*s) {
		result += std::towlower(*s++);
	}
	return result;
}

static wchar_t* char2wchar(const char* cchar)
{
	wchar_t* m_wchar;
	int len = MultiByteToWideChar(CP_ACP, 0, cchar, strlen(cchar), NULL, 0);
	m_wchar = new wchar_t[len + 1];
	MultiByteToWideChar(CP_ACP, 0, cchar, strlen(cchar), m_wchar, len);
	m_wchar[len] = '\0';
	return m_wchar;
}

static void LogGame(const char* pFormat, ...)
{
	JMP_STD(0x4068E0);
}

static void Log(const char* pFormat, ...)
{
	va_list args;
	va_start(args, pFormat);
	vsprintf_s(FinalStringBuffer, pFormat, args);
	LogGame(FinalStringBuffer);
	va_end(args);
}

static const char* Format(const char* pFormat, ...)
{
	va_list args;
	va_start(args, pFormat);
	vsprintf_s(FinalStringBuffer, pFormat, args);
	va_end(args);
	return FinalStringBuffer;
}

static void Message(const wchar_t* pFormat, ...)
{
	va_list args;
	va_start(args, pFormat);
	vswprintf_s(FinalStringBufferW, pFormat, args);
	MessageListClass::Instance->PrintMessage(FinalStringBufferW);
	va_end(args);
}

int RGB8882RGB565(int r, int g, int b)
{
	unsigned short rgb;
	unsigned char r2 = (unsigned char)r;
	unsigned char g2 = (unsigned char)g;
	unsigned char b2 = (unsigned char)b;

	r2 = r2 >> 3;
	g2 = g2 >> 2;
	b2 = b2 >> 3;

	rgb = ((r & 0b11111000) << 8) | ((g & 0b11111100) << 3) | (b >> 3);
	return (int)rgb;
}

static std::vector<TriggerClass*> match_wchar_patterns(const std::vector<TriggerClass*>& items, const wchar_t* source) {
	std::vector<TriggerClass*> result;
	std::wstring lower_source = to_lower(source);
	lower_source.erase(0, lower_source.find_first_not_of(L" \t\n\r\f\v"));
	lower_source.erase(lower_source.find_last_not_of(L" \t\n\r\f\v") + 1);

	bool exclude = false;
	if (lower_source.front() == L'!' && lower_source.back() == L'!' && lower_source.length() > 2) {
		lower_source = lower_source.substr(1, lower_source.length() - 2);
		exclude = true;
	}

	for (auto item : items) {
		if (!item || !item->Type || !item->Type->ID || !item->Type->Name)
			continue;
		std::wstring pattern = to_lower(char2wchar(Format("%s (%s)", item->Type->get_ID(), item->Type->Name)));
		if (exclude) {
			if (pattern.find(lower_source) == std::wstring::npos)
				result.push_back(item);
		}
		else {
			if (pattern.find(lower_source) != std::wstring::npos)
				result.push_back(item);
		}
	}

	return result;
}

static std::vector<TriggerClassExt> match_wchar_patternsExt(const std::vector<TriggerClassExt>& items, const wchar_t* source) {
	std::vector<TriggerClassExt> result;
	std::wstring lower_source = to_lower(source);
	lower_source.erase(0, lower_source.find_first_not_of(L" \t\n\r\f\v"));
	lower_source.erase(lower_source.find_last_not_of(L" \t\n\r\f\v") + 1);

	bool exclude = false;
	if (lower_source.front() == L'!' && lower_source.back() == L'!' && lower_source.length() > 2) {
		lower_source = lower_source.substr(1, lower_source.length() - 2);
		exclude = true;
	}

	for (auto& item : items) {
		if (!item.Type || !item.Type->ID || !item.Type->Name)
			continue;
		std::wstring pattern = to_lower(char2wchar(Format("<Expired> %s (%s)", item.Type->get_ID(), item.Type->Name)));
		if (exclude) {
			if (pattern.find(lower_source) == std::wstring::npos)
				result.push_back(item);
		}
		else {
			if (pattern.find(lower_source) != std::wstring::npos)
				result.push_back(item);
		}
	}

	return result;
}

static void GetEventList(TEventClass* pEvent, std::vector<int>& List)
{
	if (pEvent)
	{
		List.push_back((int)pEvent->EventKind);
		GetEventList(pEvent->NextEvent, List);
	}
}

static void SortTriggerArray(TriggerSort sortType)
{
	SortedTriggerArray.clear();
	SortedDestroyedTriggers.clear();
	SortedAllTriggers.clear();
	for (int i = 0; i < TriggerClass::Array->Count; i++) {
		SortedTriggerArray.push_back(TriggerClass::Array->GetItem(i));
	}

	if (bTriggerDebugEdited && !MessageListClass::Instance->HasEditFocus() && wcscmp(SearchPattern, MessageListClass::Instance->GetEditBuffer()) != 0)
	{
		bTriggerDebugEdited = false;
		CurrentPage = 0;
		wcscpy(SearchPattern, MessageListClass::Instance->GetEditBuffer());
	}
	if (wcslen(SearchPattern) > 0)
	{
		SortedTriggerArray = match_wchar_patterns(SortedTriggerArray, SearchPattern);
		SortedDestroyedTriggers = match_wchar_patternsExt(DestroyedTriggers, SearchPattern);
	}
	else
	{
		SortedDestroyedTriggers = DestroyedTriggers;
	}

	for (auto& item : SortedTriggerArray)
	{
		SortedAllTriggers.push_back(ComparableTrigger{ &item });
	}
	for (auto& item : SortedDestroyedTriggers)
	{
		SortedAllTriggers.push_back(ComparableTrigger{ &item });
	}

	switch (sortType)
	{
	case Raw:
		break;
	case ByID:
		std::sort(SortedAllTriggers.begin(), SortedAllTriggers.end(),
			[](const ComparableTrigger& lhs, const ComparableTrigger& rhs) {
			return lhs.getID() < rhs.getID();
			});
		break;
	case ByName:
		std::sort(SortedAllTriggers.begin(), SortedAllTriggers.end(),
			[](const ComparableTrigger& lhs, const ComparableTrigger& rhs) {
				return lhs.getName() < rhs.getName();
			});
		break;
	case ByTimeLeft:
		std::sort(SortedAllTriggers.begin(), SortedAllTriggers.end(),
			[](const ComparableTrigger& lhs, const ComparableTrigger& rhs) {
				return lhs.getTimeLeft() < rhs.getTimeLeft();
			});
		break;
	case ByLastExecuted:
		std::sort(SortedAllTriggers.begin(), SortedAllTriggers.end(),
			[](const ComparableTrigger& lhs, const ComparableTrigger& rhs) {
				return lhs.getLastExecuted() > rhs.getLastExecuted();
			});
		break;
	case ByDestroyed:
		std::sort(SortedAllTriggers.begin(), SortedAllTriggers.end(),
			[](const ComparableTrigger& lhs, const ComparableTrigger& rhs) {
				return lhs.getDestroyed() > rhs.getDestroyed();
			});
		break;
	default:
		break;
	}
}

class ObjectInfoClass : public CommandClass
{
public:
	//CommandClass
	virtual const char* GetName() const override
	{
		return "Display Object Info";
	}
	virtual const wchar_t* GetUIName() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_DISPLAY_OBJECT_INFO", L"Display Object Info");
	}

	virtual const wchar_t* GetUICategory() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_DEVELOPMENT", L"Development");
	}

	virtual const wchar_t* GetUIDescription() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_DISPLAY_OBJECT_INFO_DESC", L"Display objects' information along with them.");
	}

	virtual void Execute(DWORD dwUnk) const override
	{
		bObjectInfo = !bObjectInfo;
	}
};

class ObjectInfoChangeClass : public CommandClass
{
public:
	//CommandClass
	virtual const char* GetName() const override
	{
		return "Next Info Preset";
	}
	virtual const wchar_t* GetUIName() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_DISPLAY_OBJECT_INFO_NEXT", L"Next Info Preset");
	}

	virtual const wchar_t* GetUICategory() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_DEVELOPMENT", L"Development");
	}

	virtual const wchar_t* GetUIDescription() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_DISPLAY_OBJECT_INFO_NEXT_DESC", L"Change to next display object info preset.");
	}

	virtual void Execute(DWORD dwUnk) const override
	{
		ObjectInfoDisplay::ChangeNextList();
	}
};

class TriggerInfoClass : public CommandClass
{
public:
	//CommandClass
	virtual const char* GetName() const override
	{
		return "Dump Trigger Info";
	}
	virtual const wchar_t* GetUIName() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_DUMP_TRIGGER_INFO", L"Dump Trigger Info");
	}

	virtual const wchar_t* GetUICategory() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_DEVELOPMENT", L"Development");
	}

	virtual const wchar_t* GetUIDescription() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_DUMP_TRIGGER_INFO_DESC", L"Dump Trigger Info to debug.log.");
	}

	virtual void Execute(DWORD dwUnk) const override
	{
		auto DumpAllTrigger = [](TriggerClass* pTrigger) -> void {
			if (pTrigger) {
				Log("[Trigger Info]     Trigger: %s (%s), %s\n", pTrigger->Type->get_ID(), pTrigger->Type->Name, pTrigger->Enabled ? "Enabled" : "Disabled");
			}
			};
		auto DumpTrigger = [](auto self, TriggerClass* pTrigger, int counter) -> void {
			if (pTrigger) {
				Log("[Trigger Info]         Trigger: %s (%s), %s\n", pTrigger->Type->get_ID(), pTrigger->Type->Name, pTrigger->Enabled ? "Enabled" : "Disabled");
				self(self, pTrigger->NextTrigger, counter + 1);
			}
			};
		auto DumpTag = [DumpTrigger](TagClass* pTag) {
			if (pTag) {
				Log("[Trigger Info]     Tag: %s (%s)\n", pTag->Type->get_ID(), pTag->Type->Name);
				DumpTrigger(DumpTrigger, pTag->FirstTrigger, 0);
			}
			};
		auto DumpTags = [DumpTag](DynamicVectorClass<TagClass*>* tagArray) {
			for (int i = 0; i < tagArray->Count; i++) {
				DumpTag(tagArray->GetItem(i));
			}
			};

		Log("[Trigger Info] ================== Array_Logic ==================\n");
		DumpTags(TagClass::Array_Logic);
		Log("[Trigger Info] ================== Array_House ==================\n");
		for (int i = 0; i < HouseClass::Array->Count; i++) {
			if (auto pHouse = HouseClass::Array->GetItem(i)) {
				if (pHouse->RelatedTags.Count > 0) {
					Log("[Trigger Info] %s (%s):\n", pHouse->get_ID(), pHouse->PlainName);
					DumpTags(&pHouse->RelatedTags);
				}
			}
		}
		Log("[Trigger Info] ================== Array_Object ==================\n");
		std::map<TagClass*, std::vector<TechnoClass*>> ObjectTags;
		for (auto pTechno : *TechnoClass::Array) {
			if (pTechno->AttachedTag) {
				ObjectTags[pTechno->AttachedTag].push_back(pTechno);
			}
		}
		for (auto& objTag : ObjectTags) {
			for (auto& pTechno : objTag.second) {
				auto pType = pTechno->GetTechnoType();
				Log("[Trigger Info] %s, UID: %d", pType->ID, (int)pTechno->UniqueID);
				if (pTechno->IsOnMap && !pTechno->InLimbo && pTechno->IsAlive) {
					Log(", Location: (%d, %d):\n", (int)pTechno->GetMapCoords().X, (int)pTechno->GetMapCoords().Y);
				}
				else
					Log(":\n");
			}
			DumpTag(objTag.first);
		}
		Log("[Trigger Info] ================== Array_Cell ==================\n");
		std::map<TagClass*, std::vector<CellStruct>> CellTags;
		for (int i = 0; i < MapClass::Instance->TaggedCells.Count; i++) {
			auto mapCoord = MapClass::Instance->TaggedCells.GetItem(i);
			if (auto pCell = MapClass::Instance->TryGetCellAt(mapCoord)) {
				if (pCell->AttachedTag) {
					bool add = true;
					for (auto& mp : CellTags[pCell->AttachedTag]) {
						if (mp == pCell->MapCoords) {
							add = false;
						}
					}
					if (add) {
						CellTags[pCell->AttachedTag].push_back(pCell->MapCoords);
					}
				}
			}
		}
		for (auto& cellTag : CellTags) {
			for (auto& mapCoord : cellTag.second) {
				Log("[Trigger Info] Location: (%d, %d):\n", mapCoord.X, mapCoord.Y);
			}
			DumpTag(cellTag.first);
		}
		Log("[Trigger Info] ================== All_Triggers ==================\n");
		for (int i = 0; i < TriggerClass::Array->Count; i++) {
			if (auto trigger = TriggerClass::Array->GetItem(i)) {
				DumpAllTrigger(trigger);
			}
		}

		Message(L"Trigger Info Dumped");
	}
};

class TriggerExecuteLogClass : public CommandClass
{
public:
	//CommandClass
	virtual const char* GetName() const override
	{
		return "Log Trigger Execute";
	}
	virtual const wchar_t* GetUIName() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_TRIGGER_EXECUTE_LOG", L"Log Trigger Execute");
	}

	virtual const wchar_t* GetUICategory() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_DEVELOPMENT", L"Development");
	}

	virtual const wchar_t* GetUIDescription() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_TRIGGER_EXECUTE_LOG_DESC", L"Log each executed trigger's frame and name to debug.log.");
	}

	virtual void Execute(DWORD dwUnk) const override
	{
		bTriggerExecuteLog = !bTriggerExecuteLog;
	}
};

class TriggerDebugClass : public CommandClass
{
public:
	//CommandClass
	virtual const char* GetName() const override
	{
		return "Trigger Debug Mode";
	}
	virtual const wchar_t* GetUIName() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_TRIGGER_DEBUG_MODE", L"Trigger Debug Mode");
	}

	virtual const wchar_t* GetUICategory() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_DEVELOPMENT", L"Development");
	}

	virtual const wchar_t* GetUIDescription() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_TRIGGER_DEBUG_MODE_DESC", L"Enable Trigger Debug Mode.");
	}

	virtual void Execute(DWORD dwUnk) const override
	{
		bTriggerDebug = !bTriggerDebug;
	}
};

class TriggerDebugPageUpClass : public CommandClass
{
public:
	//CommandClass
	virtual const char* GetName() const override
	{
		return "Trigger Debug Page Up";
	}
	virtual const wchar_t* GetUIName() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_TRIGGER_DEBUG_PAGEUP", L"Trigger Debug Page Up");
	}

	virtual const wchar_t* GetUICategory() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_DEVELOPMENT", L"Development");
	}

	virtual const wchar_t* GetUIDescription() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_TRIGGER_DEBUG_PAGEUP_DESC", L"Trigger Debug Page Up.");
	}

	virtual void Execute(DWORD dwUnk) const override
	{
		if (bTriggerDebug && CurrentPage > 0)
		{
			CurrentPage--;
		}
	}
};

class TriggerDebugPageDownClass : public CommandClass
{
public:
	//CommandClass
	virtual const char* GetName() const override
	{
		return "Trigger Debug Page Down";
	}
	virtual const wchar_t* GetUIName() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_TRIGGER_DEBUG_PAGEDOWN", L"Trigger Debug Page Down");
	}

	virtual const wchar_t* GetUICategory() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_DEVELOPMENT", L"Development");
	}

	virtual const wchar_t* GetUIDescription() const override
	{
		return GeneralUtils::LoadStringUnlessMissing("TXT_TRIGGER_DEBUG_PAGEDOWN_DESC", L"Trigger Debug Page Down.");
	}

	virtual void Execute(DWORD dwUnk) const override
	{
		if (bTriggerDebug && !bTriggerDebugPageEnd)
		{
			CurrentPage++;
		}
	}
};
DEFINE_HOOK(533066, CommandClassCallback_Register, 6)
{
	// Load it after Ares'

	MakeCommand<ObjectInfoClass>();
	MakeCommand<ObjectInfoChangeClass>();
	MakeCommand<TriggerInfoClass>();
	MakeCommand<TriggerExecuteLogClass>();
	MakeCommand<TriggerDebugClass>();
	MakeCommand<TriggerDebugPageUpClass>();
	MakeCommand<TriggerDebugPageDownClass>();

	return 0;
}

DEFINE_HOOK(4F4583, GScreenClass_DrawOnTop_TheDarkSideOfTheMoon, 6)
{
	if (bObjectInfo)
	{
		auto getMissionName = [](int mID)
			{
				switch (mID)
				{
				case -1:
					return "None";
				case 0:
					return "Sleep";
				case 1:
					return "Attack";
				case 2:
					return "Move";
				case 3:
					return "QMove";
				case 4:
					return "Retreat";
				case 5:
					return "Guard";
				case 6:
					return "Sticky";
				case 7:
					return "Enter";
				case 8:
					return "Capture";
				case 9:
					return "Eaten";
				case 10:
					return "Harvest";
				case 11:
					return "Area_Guard";
				case 12:
					return "Return";
				case 13:
					return "Stop";
				case 14:
					return "Ambush";
				case 15:
					return "Hunt";
				case 16:
					return "Unload";
				case 17:
					return "Sabotage";
				case 18:
					return "Construction";
				case 19:
					return "Selling";
				case 20:
					return "Repair";
				case 21:
					return "Rescue";
				case 22:
					return "Missile";
				case 23:
					return "Harmless";
				case 24:
					return "Open";
				case 25:
					return "Patrol";
				case 26:
					return "ParadropApproach";
				case 27:
					return "ParadropOverfly";
				case 28:
					return "Wait";
				case 29:
					return "AttackMove";
				case 30:
					return "SpyplaneApproach";
				case 31:
					return "SpyplaneOverfly";
				default:
					return "INVALID_MISSION";
				}
			};

		for (auto pTechno : *TechnoClass::Array)
		{
			if (pTechno->unknown_bool_431 || pTechno->IsSelected)
			{
				Point2D position;
				TacticalClass::Instance->CoordsToClient(&pTechno->GetCoords(), &position);

				int offsetX = position.X + ObjectInfoDisplay::DisplayOffsetX;
				int offsetY = position.Y + ObjectInfoDisplay::DisplayOffsetY;
				bool opposite = false;
				bool draw = true;

				if (DSurface::Composite->GetHeight() - position.Y < 150)
				{
					opposite = true;
					offsetY = position.Y - 30;
				}
				if ((position.X < -80 || position.X > DSurface::Composite->GetWidth() + 80)
					|| (position.Y < -80 || position.Y > DSurface::Composite->GetHeight() + 80))
					draw = false;

				char displayBuffer[0x100] = { 0 };

				auto DrawText = [&opposite, &draw](const wchar_t* string, int& offsetX, int& offsetY, int color) {
					if (draw)
					{
						auto h = DSurface::Composite->GetHeight();
						auto w = DSurface::Composite->GetWidth();

						auto wanted = Drawing::GetTextDimensions(string);
						wanted.Height += 2;

						int exceedX = w - offsetX - wanted.Width;
						if (exceedX >= 0)
							exceedX = 0;

						int exceedY = h - offsetY - wanted.Height;
						if (exceedY >= 0)
							exceedY = 0;

						RectangleStruct rect = { offsetX + exceedX, offsetY, wanted.Width, wanted.Height };

						DSurface::Composite->FillRect(&rect, COLOR_BLACK);
						DSurface::Composite->DrawTextA(string, rect.X, rect.Y, color);

						if (opposite)
							offsetY -= wanted.Height;
						else
							offsetY += wanted.Height;
					}
					};
				auto DrawToolTipText = [&opposite, &draw](const wchar_t* string, int& offsetX, int& offsetY, int color) {
					if (draw)
					{
						wchar_t string2[0x800];
						swprintf_s(string2, L"%s", string);
						wchar_t string3[0x800];
						swprintf_s(string3, L"%s", string);
						auto h = DSurface::Composite->GetHeight();
						auto w = DSurface::Composite->GetWidth();
						RectangleStruct wanted = { 0, 0, 0, 0 };


						const wchar_t* d = L"\n";
						wchar_t* p;
						p = wcstok(string3, d);
						while (p)
						{
							auto wanted2 = Drawing::GetTextDimensions(p);
							wanted.Height += wanted2.Height + 2;
							if (wanted.Width < wanted2.Width)
								wanted.Width = wanted2.Width;
							p = wcstok(NULL, d);
						}

						int exceedX = w - offsetX - wanted.Width;
						if (exceedX >= 0)
							exceedX = 0;

						int exceedY = h - offsetY - wanted.Height;
						if (exceedY >= 0)
							exceedY = 0;

						RectangleStruct rect = { offsetX + exceedX + 4, offsetY , wanted.Width, wanted.Height };
						RectangleStruct rectBack = { rect.X - 3, rect.Y - 1, rect.Width + 6, rect.Height + 2 };
						RectangleStruct rectLine = { rectBack.X - 1, rectBack.Y - 1, rectBack.Width + 2, rectBack.Height + 2 };

						DSurface::Composite->FillRect(&rectLine, color);
						DSurface::Composite->FillRect(&rectBack, COLOR_BLACK);

						p = wcstok(string2, d);
						while (p)
						{
							auto wanted2 = Drawing::GetTextDimensions(p);
							DSurface::Composite->DrawTextA(p, rect.X, rect.Y, color);
							rect.Y += wanted2.Height + 2;
							p = wcstok(NULL, d);
						}

						wanted.Height += 2;

						if (opposite)
							offsetY -= wanted.Height;
						else
							offsetY += wanted.Height;
					}
					};

				auto append = [&displayBuffer](const char* pFormat, ...)
					{
						char buffer[0x100];
						va_list args;
						va_start(args, pFormat);
						vsprintf(buffer, pFormat, args);
						va_end(args);
						strcat_s(displayBuffer, buffer);
					};
				auto display = [&displayBuffer, &DrawText, &offsetX, &offsetY]()
					{
						wchar_t bufferW[0x100] = { 0 };
						mbstowcs(bufferW, displayBuffer, strlen(displayBuffer));
						DrawText(bufferW, offsetX, offsetY, COLOR_WHITE);

						displayBuffer[0] = 0;
					};
				auto displayWide = [&DrawText, &offsetX, &offsetY](const wchar_t* pFormat, ...)
					{
						wchar_t buffer[0x100] = { 0 };
						va_list args;
						va_start(args, pFormat);
						vswprintf_s(buffer, 0x100, pFormat, args);
						va_end(args);

						DrawText(buffer, offsetX, offsetY, COLOR_WHITE);
					};
				auto displayToopTip = [&DrawToolTipText, &offsetX, &offsetY, &pTechno](const wchar_t* pFormat, ...)
					{
						wchar_t buffer[0x100] = { 0 };
						va_list args;
						va_start(args, pFormat);
						vswprintf_s(buffer, 0x100, pFormat, args);
						va_end(args);

						auto color = pTechno->Owner->Color;
						DrawToolTipText(buffer, offsetX, offsetY, RGB8882RGB565(color.R, color.G, color.B));

					};


				auto printFoots = [&append, &display, &displayWide, &displayToopTip, &getMissionName](FootClass* pFoot)
					{
						auto pType = pFoot->GetTechnoType();

						for (const auto& name : ObjectInfoDisplay::GetList())
						{
							bool allDisplay = false;
							if (name == "NONEALL")
								allDisplay = true;

							if (ObjectInfoDisplay::CanDisplay("uiname", name) || allDisplay)
							{
								char missing[0x100] = { 0 };
								sprintf(missing, "MISSING:'%s'", pType->UINameLabel);
								displayToopTip(L"%s", GeneralUtils::LoadStringUnlessMissing(pType->UINameLabel, char2wchar(missing)));
							}
							if (ObjectInfoDisplay::CanDisplay("id", name) || allDisplay)
							{
								append("ID = %s", pType->ID);
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("uid", name) || allDisplay)
							{
								append("UID = %d", (int)pFoot->UniqueID);
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("hp", name) || allDisplay)
							{
								append("HP = (%d / %d)", pFoot->Health, pType->Strength);
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("owner", name) || allDisplay)
							{
								append("Owner = %s (%s)", pFoot->Owner->get_ID(), pFoot->Owner->PlainName);
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("location", name) || allDisplay)
							{
								append("Location = (%d, %d)", (int)pFoot->GetMapCoords().X, (int)pFoot->GetMapCoords().Y);
								display();
							}
							if (pFoot->HasAnyLink())
							{
								if (ObjectInfoDisplay::CanDisplay("link", name) || allDisplay)
								{

									for (auto i = 0; i < pFoot->RadioLinks.Capacity; ++i)
									{
										if (auto const pLink = pFoot->GetNthLink(i))
										{
											auto pLinkType = pLink->GetType();
											append("Link %d: UID = %d, ID = %s, Location = (%d, %d)",
												i,
												(int)pLink->UniqueID,
												pLinkType->ID,
												(int)pLink->GetMapCoords().X, (int)pLink->GetMapCoords().Y
											); 
											display();
										}
									}
								}
							}

							if (pFoot->BelongsToATeam())
							{
								auto pTeam = pFoot->Team;
								auto pTeamType = pFoot->Team->Type;
								if (ObjectInfoDisplay::CanDisplay("aitrigger", name) || allDisplay)
								{
									bool found = false;
									for (int i = 0; i < AITriggerTypeClass::Array->Count && !found; i++)
									{
										auto pTriggerTeam1Type = AITriggerTypeClass::Array->GetItem(i)->Team1;
										auto pTriggerTeam2Type = AITriggerTypeClass::Array->GetItem(i)->Team2;

										if (pTeamType && ((pTriggerTeam1Type && pTriggerTeam1Type == pTeamType) || (pTriggerTeam2Type && pTriggerTeam2Type == pTeamType)))
										{
											found = true;
											auto pTriggerType = AITriggerTypeClass::Array->GetItem(i);
											append("Trigger ID = %s, weights [Current, Min, Max]: %f, %f, %f", pTriggerType->ID, pTriggerType->Weight_Current, pTriggerType->Weight_Minimum, pTriggerType->Weight_Maximum);
										}
									}
									if (found)
										display();
								}
								if (ObjectInfoDisplay::CanDisplay("team", name) || allDisplay)
								{
									append("Team ID = %s, Script ID = %s, Taskforce ID = %s",
										pTeam->Type->ID, pTeam->CurrentScript->Type->get_ID(), pTeam->Type->TaskForce->ID);

									display();
								}
								if (ObjectInfoDisplay::CanDisplay("currentscript", name) || allDisplay)
								{
									bool missingUnit = false;
									for (int i = 0; i < 6; ++i)
									{
										auto pEntry = pTeam->Type->TaskForce->Entries[i];
										if (pEntry.Type && pEntry.Amount > 0)
										{
											int missing = pEntry.Amount;

											for (auto pUnit = pTeam->FirstUnit; pUnit; pUnit = pUnit->NextTeamMember)
											{
												auto pUnitType = pUnit->GetTechnoType();

												if (pUnitType
													&& pUnit->IsAlive
													&& pUnit->Health > 0
													&& !pUnit->InLimbo)
												{
													if (pEntry.Type->ID == pUnitType->ID)
													{
														missing--;
													}
												}
											}

											if (missing > 0)
											{
												missingUnit = true;
											}
										}
									}
									if (pTeam->CurrentScript->idxCurrentLine >= 0)
									{
										ScriptActionNode sNode;
										pTeam->CurrentScript->GetCurrentAction(&sNode);
										append("Current Script [Line = Action, Argument]: %d = %d, %d", pTeam->CurrentScript->idxCurrentLine, sNode.Action, sNode.Argument);

									}
									else if (!missingUnit)
										append("Current Script [Line = Action, Argument]: %d", pTeam->CurrentScript->idxCurrentLine);
									else
									{
										append("Team Missing: ", pTeam->CurrentScript->idxCurrentLine);
										for (int i = 0; i < 6; ++i)
										{
											auto pEntry = pTeam->Type->TaskForce->Entries[i];
											if (pEntry.Type && pEntry.Amount > 0)
											{
												int missing = pEntry.Amount;

												for (auto pUnit = pTeam->FirstUnit; pUnit; pUnit = pUnit->NextTeamMember)
												{
													auto pUnitType = pUnit->GetTechnoType();

													if (pUnitType
														&& pUnit->IsAlive
														&& pUnit->Health > 0
														&& !pUnit->InLimbo)
													{
														if (pEntry.Type->ID == pUnitType->ID)
														{
															missing--;
														}
													}
												}

												if (missing > 0)
												{
													append("(%d, %s) ", missing, pEntry.Type->ID);
												}
											}
										}
									}
									display();
								}
							}
							if (ObjectInfoDisplay::CanDisplay("passenger", name) || allDisplay)
							{
								if (pFoot->Passengers.NumPassengers > 0)
								{
									FootClass* pCurrent = pFoot->Passengers.FirstPassenger;
									append("%d Passengers: %s", pFoot->Passengers.NumPassengers, pCurrent->GetTechnoType()->ID);
									for (pCurrent = abstract_cast<FootClass*>(pCurrent->NextObject); pCurrent; pCurrent = abstract_cast<FootClass*>(pCurrent->NextObject))
										append(", %s", pCurrent->GetTechnoType()->ID);
									display();
								}
							}
							if (ObjectInfoDisplay::CanDisplay("target", name) || allDisplay)
							{
								if (pFoot->Target)
								{
									auto mapCoords = CellStruct::Empty;
									auto ID = "N/A";

									if (auto const pObject = abstract_cast<ObjectClass*>(pFoot->Target))
									{
										mapCoords = pObject->GetMapCoords();
										ID = pObject->GetType()->get_ID();
									}
									else if (auto const pCell = abstract_cast<CellClass*>(pFoot->Target))
									{
										mapCoords = pCell->MapCoords;
										ID = "Cell";
									}

									append("Target = %s, Distance = %d, Location = (%d, %d)", ID, (pFoot->DistanceFrom(pFoot->Target) / 256), mapCoords.X, mapCoords.Y);
									display();
								}
							}
							if (ObjectInfoDisplay::CanDisplay("destination", name) || allDisplay)
							{
								auto pDestination = abstract_cast<TechnoClass*>(pFoot->Destination);

								if (pDestination)
								{
									append("Destination = %s, Distance = %d, Location = (%d, %d)", pDestination->GetTechnoType()->ID, (pDestination->DistanceFrom(pFoot) / 256), pDestination->GetMapCoords().X, pDestination->GetMapCoords().Y);
									display();
								}
								else
								{
									if (pFoot->Destination)
									{
										auto destCell = CellClass::Coord2Cell(pFoot->Destination->GetCoords());
										append("Destination = (%d, %d)", destCell.X, destCell.Y);
										display();
									}
								}
							}
							if (ObjectInfoDisplay::CanDisplay("focus", name) || allDisplay)
							{
								auto pFocus = abstract_cast<TechnoClass*>(pFoot->Focus);

								if (pFocus)
								{
									append("Focus = %s, Distance = %d, Location = (%d, %d)", pFocus->GetTechnoType()->ID, (pFocus->DistanceFrom(pFoot) / 256), pFocus->GetMapCoords().X, pFocus->GetMapCoords().Y);
									display();
								}
							}

							if (ObjectInfoDisplay::CanDisplay("ammo", name) || allDisplay)
							{
								if (pType->Ammo > 0)
								{
									append("Ammo = (%d / %d)", pFoot->Ammo, pType->Ammo);
									display();
								}
							}
							if (ObjectInfoDisplay::CanDisplay("currentmission", name) || allDisplay)
							{
								append("Current Mission = %d (%s)", pFoot->CurrentMission, getMissionName((int)pFoot->CurrentMission));
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("megamission", name) || allDisplay)
							{
								if (pFoot->unknown_int_5C4 > -1)
								{
									append("Mega Mission = %d (%s)", pFoot->unknown_int_5C4, getMissionName((int)pFoot->unknown_int_5C4));
									display();
								}

							}
							if (ObjectInfoDisplay::CanDisplay("megatarget", name) || allDisplay)
							{
								auto megaTarget = (AbstractClass*)pFoot->unknown_5CC;
								if (megaTarget)
								{
									auto mapCoords = CellStruct::Empty;
									auto ID = "N/A";

									if (auto const pObject = abstract_cast<ObjectClass*>(megaTarget))
									{
										mapCoords = pObject->GetMapCoords();
										ID = pObject->GetType()->get_ID();
									}
									else if (auto const pCell = abstract_cast<CellClass*>(megaTarget))
									{
										mapCoords = pCell->MapCoords;
										ID = "Cell";
									}

									append("Mega Target = %s, Distance = %d, Location = (%d, %d)", ID, (pFoot->DistanceFrom(megaTarget) / 256), mapCoords.X, mapCoords.Y);
									display();
								}
							}
							if (ObjectInfoDisplay::CanDisplay("megadestination", name) || allDisplay)
							{
								auto megaDestination = (AbstractClass*)pFoot->unknown_5C8;
								auto pDestination = abstract_cast<TechnoClass*>(megaDestination);
								if (pDestination)
								{
									append("Destination = %s, Distance = %d, Location = (%d, %d)", pDestination->GetTechnoType()->ID, (pDestination->DistanceFrom(pFoot) / 256), pDestination->GetMapCoords().X, pDestination->GetMapCoords().Y);
									display();
								}
								else
								{
									if (megaDestination)
									{
										auto destCell = CellClass::Coord2Cell(megaDestination->GetCoords());
										append("Mega Destination = (%d, %d)", destCell.X, destCell.Y);
										display();
									}
								}
							}

							if (ObjectInfoDisplay::CanDisplay("group", name) || allDisplay)
							{
								append("Group = %d", pFoot->Group);
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("recruit", name) || allDisplay)
							{
								append("RecruitA = %d, RecruitB = %d", (int)pFoot->RecruitableA, (int)pFoot->RecruitableB);
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("veterancy", name) || allDisplay)
							{
								if (pType->Trainable)
								{
									char veterancy[10] = { 0 };
									if (pFoot->Veterancy.IsRookie())
										sprintf(veterancy, "%s", "Rookie");
									else if (pFoot->Veterancy.IsVeteran())
										sprintf(veterancy, "%s", "Veteran");
									else if (pFoot->Veterancy.IsElite())
										sprintf(veterancy, "%s", "Elite");
									else
										sprintf(veterancy, "%s", "N/A");
									double truncatedVet = floor(pFoot->Veterancy.Veterancy * 100) / 100.0;
									append("Veterancy = %s (%.2f)", veterancy, truncatedVet);
									display();
								}
							}
							if (ObjectInfoDisplay::CanDisplay("tag", name) || allDisplay)
							{
								if (pFoot->AttachedTag)
								{
									append("Tag = %s, InstanceCount = %d", pFoot->AttachedTag->Type->get_ID(), pFoot->AttachedTag->InstanceCount);
									display();
									if (pFoot->AttachedTag->FirstTrigger)
									{
										auto pTrigger = pFoot->AttachedTag->FirstTrigger;

										auto displayTrigger = [append, display](auto self, TriggerClass* trigger, int counter) -> void {
											if (trigger) {
												append("Trigger %d = %s", counter, trigger->Type->get_ID());
												display();
												self(self, trigger->NextTrigger, counter + 1);
											}
											};
										displayTrigger(displayTrigger, pTrigger, 0);
									}
								}
							}
						}
					};
				auto printBuilding = [&append, &display, &displayWide, &displayToopTip, &getMissionName](BuildingClass* pBuilding)
					{
						auto pType = pBuilding->GetTechnoType();

						for (const auto& name : ObjectInfoDisplay::GetList())
						{
							bool allDisplay = false;
							if (name == "NONEALL")
								allDisplay = true;

							if (ObjectInfoDisplay::CanDisplay("uiname", name) || allDisplay)
							{
								char missing[0x100] = { 0 };
								sprintf(missing, "MISSING:'%s'", pType->UINameLabel);
								displayToopTip(L"%s", GeneralUtils::LoadStringUnlessMissing(pType->UINameLabel, char2wchar(missing)));
							}
							if (ObjectInfoDisplay::CanDisplay("id", name) || allDisplay)
							{
								append("ID = %s", pType->ID);
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("uid", name) || allDisplay)
							{
								append("UID = %d", (int)pBuilding->UniqueID);
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("hp", name) || allDisplay)
							{
								append("HP = (%d / %d)", pBuilding->Health, pBuilding->Type->Strength);
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("owner", name) || allDisplay)
							{
								append("Owner = %s (%s)", pBuilding->Owner->get_ID(), pBuilding->Owner->PlainName);
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("location", name) || allDisplay)
							{
								append("Location = (%d, %d)", pBuilding->GetMapCoords().X, pBuilding->GetMapCoords().Y);
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("power", name) || allDisplay)
							{

								if (auto const pBldType = abstract_cast<BuildingTypeClass*>(pType))
								{
									int power = pBldType->PowerBonus - pBldType->PowerDrain;
									if (power > 0)
									{
										power *= ((double)pBuilding->Health / (double)pBuilding->Type->Strength);
									}
									append("Power: %d", power);
									append(", Total Power Output: %d, Total Power Drain: %d", pBuilding->Owner->PowerOutput, pBuilding->Owner->PowerDrain);
									display();
								}
							}
							if (ObjectInfoDisplay::CanDisplay("factory", name) || allDisplay)
							{
								if (pBuilding->Factory && pBuilding->Factory->Object)
								{
									append("Production: %s (%d%%)", pBuilding->Factory->Object->GetTechnoType()->ID, (pBuilding->Factory->GetProgress() * 100 / 54));
									display();
								}
							}
							if (ObjectInfoDisplay::CanDisplay("money", name) || allDisplay)
							{
								//if (pBuilding->Type->Refinery || pBuilding->Type->ResourceGatherer)
								{
									append("Money: %d", pBuilding->Owner->Available_Money());
									display();
								}
							}
							if (ObjectInfoDisplay::CanDisplay("occupants", name) || allDisplay)
							{
								if (pBuilding->Occupants.Count > 0)
								{
									append("%d Occupants: %s", pBuilding->Occupants.Count, pBuilding->Occupants.GetItem(0)->Type->ID);
									for (int i = 1; i < pBuilding->Occupants.Count; i++)
									{
										append(", %s", pBuilding->Occupants.GetItem(i)->Type->ID);
									}
									display();
								}
							}
							if (pBuilding->HasAnyLink())
							{
								if (ObjectInfoDisplay::CanDisplay("link", name) || allDisplay)
								{

									for (auto i = 0; i < pBuilding->RadioLinks.Capacity; ++i)
									{
										if (auto const pLink = pBuilding->GetNthLink(i))
										{
											auto pLinkType = pLink->GetType();
											append("Link %d: UID = %d, ID = %s, Location = (%d, %d)",
												i,
												(int)pLink->UniqueID,
												pLinkType->ID,
												(int)pLink->GetMapCoords().X, (int)pLink->GetMapCoords().Y
											);
											display();
										}
									}
								}
							}
							if (ObjectInfoDisplay::CanDisplay("enemy", name) || allDisplay)
							{
								HouseClass* pEnemyHouse = nullptr;

								if (auto pHouse = pBuilding->Owner)
								{
									int angerLevel = -1;
									if (pHouse->EnemyHouseIndex >= 0)
									{
										pEnemyHouse = HouseClass::Array->GetItem(pHouse->EnemyHouseIndex);
										for (auto pNode : pHouse->AngerNodes)
										{
											if (pNode.House == pEnemyHouse)
											{
												angerLevel = pNode.AngerLevel;
											}
										}
									}
									else
									{
										for (auto pNode : pHouse->AngerNodes)
										{
											if (!pNode.House->Type->MultiplayPassive
												&& !pNode.House->Defeated
												&& !pNode.House->IsObserver()
												&& !pNode.House->IsAlliedWith(pHouse)
												&& ((pNode.AngerLevel > angerLevel
													)
													|| angerLevel < 0))
											{
												angerLevel = pNode.AngerLevel;
												pEnemyHouse = pNode.House;
											}
										}
									}

									if (pEnemyHouse)
									{
										append("Enemy = %s (%s),  AngerLevel = %d", pEnemyHouse->get_ID(), pEnemyHouse->PlainName, angerLevel);
										display();
									}
								}
							}
							if (ObjectInfoDisplay::CanDisplay("upgrades", name) || allDisplay)
							{
								if (pBuilding->Type->Upgrades)
								{
									append("Upgrades (%d / %d): ", pBuilding->UpgradeLevel, pBuilding->Type->Upgrades);
									for (int i = 0; i < 3; i++)
									{
										if (i != 0)
											append(", ");

										append("Slot %d = %s", i + 1, pBuilding->Upgrades[i] ? pBuilding->Upgrades[i]->get_ID() : "<none>");
									}
									display();
								}
							}
							if (ObjectInfoDisplay::CanDisplay("target", name) || allDisplay)
							{
								if (pBuilding->Target)
								{
									auto mapCoords = CellStruct::Empty;
									auto ID = "N/A";

									if (auto const pObject = abstract_cast<ObjectClass*>(pBuilding->Target))
									{
										mapCoords = pObject->GetMapCoords();
										ID = pObject->GetType()->get_ID();
									}
									else if (auto const pCell = abstract_cast<CellClass*>(pBuilding->Target))
									{
										mapCoords = pCell->MapCoords;
										ID = "Cell";
									}

									append("Target = %s, Distance = %d, Location = (%d, %d)", ID, (pBuilding->DistanceFrom(pBuilding->Target) / 256), mapCoords.X, mapCoords.Y);
									display();
								}
							}
							if (ObjectInfoDisplay::CanDisplay("ammo", name) || allDisplay)
							{
								if (pBuilding->Type->Ammo > 0)
								{
									append("Ammo = (%d / %d)", pBuilding->Ammo, pBuilding->Type->Ammo);
									display();
								}
							}
							if (ObjectInfoDisplay::CanDisplay("currentmission", name) || allDisplay)
							{
								append("Current Mission = %d (%s)", pBuilding->CurrentMission, getMissionName((int)pBuilding->CurrentMission));
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("group", name) || allDisplay)
							{
								append("Group = %d", pBuilding->Group);
								display();
							}
							if (ObjectInfoDisplay::CanDisplay("veterancy", name) || allDisplay)
							{
								if (pType->Trainable)
								{
									char veterancy[10] = { 0 };
									if (pBuilding->Veterancy.IsRookie())
										sprintf(veterancy, "%s", "Rookie");
									else if (pBuilding->Veterancy.IsVeteran())
										sprintf(veterancy, "%s", "Veteran");
									else if (pBuilding->Veterancy.IsElite())
										sprintf(veterancy, "%s", "Elite");
									else
										sprintf(veterancy, "%s", "N/A");
									append("Veterancy = %s (%.2f)", veterancy, pBuilding->Veterancy.Veterancy);
									display();
								}
							}
							if (ObjectInfoDisplay::CanDisplay("tag", name) || allDisplay)
							{
								if (pBuilding->AttachedTag)
								{
									append("Tag = (%s), InstanceCount = (%d)", pBuilding->AttachedTag->Type->get_ID(), pBuilding->AttachedTag->InstanceCount);
									display();
								}
							}
						}

					};


				switch (pTechno->WhatAmI())
				{
				case AbstractType::Infantry:
				case AbstractType::Unit:
				case AbstractType::Aircraft:
					printFoots(static_cast<FootClass*>(pTechno));
					break;
				case AbstractType::Building:
					printBuilding(static_cast<BuildingClass*>(pTechno));
					break;
				default:
					append("INVALID ITEM!");
					display();
					break;
				}
			}
		}
	}

	if (bTriggerDebug)
	{
		auto DrawText = [](const wchar_t* string, int& offsetX, int& offsetY, int color, int index, int bkgColor) {

			auto h = DSurface::Composite->GetHeight();
			auto w = DSurface::Composite->GetWidth();


			auto wanted = Drawing::GetTextDimensions(string);
			wanted.Height += 2;

			if (wanted.Height + offsetY >= h - 100)
				return false;

			RectangleStruct rect = { offsetX , offsetY, wanted.Width, wanted.Height };
			TriggerDebugRect[index] = rect;

			DSurface::Composite->FillRect(&rect, bkgColor);
			DSurface::Composite->DrawTextA(string, rect.X, rect.Y, color);

			offsetY += wanted.Height;
			return true;
			};

		SortTriggerArray(Sort);
		int DisplayX = TriggerDebugStartX;
		int DisplayY = TriggerDebugStartY;
		for (int i = 0; i < RECT_COUNT; ++i)
		{
			TriggerDebugRect[i] = { 0,0,0,0 };
		}
		bTriggerDebugPageEnd = true;

		if (bTriggerDebugTimerEdited && !MessageListClass::Instance->HasEditFocus())
		{
			bTriggerDebugTimerEdited = false;
			ChangedTimer = _wtoi(MessageListClass::Instance->GetEditBuffer());
		}

		for (int i = CurrentPage * PageTriggerCount; i < std::min((int)SortedAllTriggers.size(), (CurrentPage + 1) * PageTriggerCount); i++) {
			std::string text;
			bool enabled = false;
			auto& obj = SortedAllTriggers[i];
			if (std::holds_alternative<TriggerClass**>(obj.item))
			{
				auto trigger = *std::get<TriggerClass**>(obj.item);
				auto& ext = TriggerExtMap[trigger];
				if (trigger->Enabled)
					enabled = true;
				text = Format("%s (%s)", trigger->Type->get_ID(), trigger->Type->Name);
				if (bTriggerDebugDetailed)
				{
					int timeLeft = trigger->Enabled ? trigger->Timer.GetTimeLeft() : trigger->Timer.TimeLeft;
					if (timeLeft > 0)
					{
						text += Format(", Frame Left(s): %d(%d)", timeLeft, timeLeft / 15);
						if (ext.ResetTimer > -1)
							text += " (Modified)";
					}
					int lastExecutedFrame = ext.LastExecutedFrame;
					if (lastExecutedFrame > -1)
					{
						text += Format(", Last Executed Frame(s): %d(%d)", lastExecutedFrame, lastExecutedFrame / 15);
					}
					int executedCount = ext.ExecutedCount;
					if (executedCount > 0)
					{
						text += Format(", Execute Count: %d", executedCount);
					}
					if (trigger->Enabled)
					{
						if (!ext.OccuredEvents.empty())
						{
							std::vector<int> eventList;
							GetEventList(trigger->Type->FirstEvent, eventList);
							text += ", Conditions:";
							for (int i = eventList.size() - 1; i >= 0; --i)
							{
								text += Format(" %d[%s]", eventList[i], ext.OccuredEvents[i] ? L"@" : L"  ");
							}
						}
					}
				}
			}
			else
			{
				auto& trigger = *std::get<TriggerClassExt*>(obj.item);
				text = Format("<Expired> %s (%s)", trigger.Type->get_ID(), trigger.Type->Name);
				if (bTriggerDebugDetailed)
				{
					int lastExecutedFrame = trigger.LastExecutedFrame;
					if (lastExecutedFrame > -1)
					{
						text += Format(", Last Executed Frame(s): %d(%d)", lastExecutedFrame, lastExecutedFrame / 15);
					}
					int executedCount = trigger.ExecutedCount;
					if (executedCount > 0)
					{
						text += Format(", Execute Count: %d", executedCount);
					}
					int destroyedFrame = trigger.DestroyedFrame;
					if (destroyedFrame > 0)
					{
						text += Format(", Expired Frame(s): %d(%d)", destroyedFrame, destroyedFrame / 15);
					}
				}
			}
					
			if (!DrawText(char2wchar(text.c_str()),
				DisplayX, DisplayY, COLOR_WHITE, i - CurrentPage * PageTriggerCount, enabled ? RGB8882RGB565(0,120,0) : COLOR_BLACK))
			{
				if (PageTriggerCount == RECT_COUNT)
					PageTriggerCount = i - CurrentPage * PageTriggerCount;
				bTriggerDebugPageEnd = false;
				break;
			}
			if (i == (CurrentPage + 1) * PageTriggerCount - 1 && i != SortedAllTriggers.size() - 1)
				bTriggerDebugPageEnd = false;
		}


		const wchar_t* pageUp = L"Page Up";
		const wchar_t* pageDown = L"Page Down";
		const wchar_t* Detail = L"Details";
		std::wstring SortType = L"Sort: ";
		switch (Sort)
		{
		case Raw:
			SortType += L"Raw";
			break;
		case ByID:
			SortType += L"ID";
			break;
		case ByName:
			SortType += L"Name";
			break;
		case ByTimeLeft:
			SortType += L"Frame Left";
			break;
		case ByLastExecuted:
			SortType += L"Last Executed";
			break;
		case ByDestroyed:
			SortType += L"Expired";
			break;
		default:
			break;
		}
		const wchar_t* Search = L"Search";
		const wchar_t* EnableChanged = L"Enable Timer-modified";
		int upRight = 0;
		{
			auto wanted = Drawing::GetTextDimensions(pageUp);
			wanted.Height += 2;

			RectangleStruct rect = { TriggerDebugStartX , TriggerDebugStartY - wanted.Height, wanted.Width, wanted.Height };
			TriggerDebugPageUp = rect;
			upRight = TriggerDebugStartX + wanted.Width;

			DSurface::Composite->FillRect(&rect, COLOR_BLACK);
			DSurface::Composite->DrawTextA(pageUp, rect.X, rect.Y, COLOR_WHITE);
		}
		{
			auto wanted = Drawing::GetTextDimensions(pageDown);
			wanted.Height += 2;

			RectangleStruct rect = { upRight + 10 , TriggerDebugStartY - wanted.Height, wanted.Width, wanted.Height };
			TriggerDebugPageDown = rect;
			upRight = rect.X + wanted.Width;

			DSurface::Composite->FillRect(&rect, COLOR_BLACK);
			DSurface::Composite->DrawTextA(pageDown, rect.X, rect.Y, COLOR_WHITE);
		}
		{
			auto wanted = Drawing::GetTextDimensions(Detail);
			wanted.Height += 2;

			RectangleStruct rect = { upRight + 10 , TriggerDebugStartY - wanted.Height, wanted.Width, wanted.Height };
			TriggerDebugDetailed = rect;
			upRight = rect.X + wanted.Width;

			DSurface::Composite->FillRect(&rect, COLOR_BLACK);
			DSurface::Composite->DrawTextA(Detail, rect.X, rect.Y, bTriggerDebugDetailed ? COLOR_RED : COLOR_WHITE);
		}
		{
			auto wanted = Drawing::GetTextDimensions(SortType.c_str());
			wanted.Height += 2;

			RectangleStruct rect = { upRight + 10 , TriggerDebugStartY - wanted.Height, wanted.Width, wanted.Height };
			TriggerDebugSort = rect;
			upRight = rect.X + wanted.Width;

			DSurface::Composite->FillRect(&rect, COLOR_BLACK);
			DSurface::Composite->DrawTextA(SortType.c_str(), rect.X, rect.Y, COLOR_WHITE);
		}
		{
			auto wanted = Drawing::GetTextDimensions(Search);
			wanted.Height += 2;

			RectangleStruct rect = { upRight + 10 , TriggerDebugStartY - wanted.Height, wanted.Width, wanted.Height };
			TriggerDebugSearch = rect;
			upRight = rect.X + wanted.Width;

			DSurface::Composite->FillRect(&rect, COLOR_BLACK);
			DSurface::Composite->DrawTextA(Search, rect.X, rect.Y, COLOR_WHITE);
		}		
		{
			auto wanted = Drawing::GetTextDimensions(EnableChanged);
			wanted.Height += 2;

			RectangleStruct rect = { upRight + 10 , TriggerDebugStartY - wanted.Height, wanted.Width, wanted.Height };
			TriggerDebugEnableModified = rect;
			upRight = rect.X + wanted.Width;

			DSurface::Composite->FillRect(&rect, COLOR_BLACK);
			DSurface::Composite->DrawTextA(EnableChanged, rect.X, rect.Y, COLOR_WHITE);
		}

		const wchar_t* Modes[Count] =
		{
			L"Run", L"Enable", L"Disable", L"Destroy", L"Set Timer"
		};
		int modesRight[Count]{ 0 };
		for (int i = 0; i < Count; ++i)
		{
			auto wanted = Drawing::GetTextDimensions(Modes[i]);
			wanted.Height += 2;

			RectangleStruct rect = { TriggerDebugStartX , TriggerDebugPageDown.Y - wanted.Height, wanted.Width, wanted.Height };
			if (i > 0)
			{
				rect.X = modesRight[i - 1] + 10;
			}
			TriggerDebugMode[i] = rect;
			modesRight[i] = rect.X + wanted.Width;

			DSurface::Composite->FillRect(&rect, COLOR_BLACK);
			DSurface::Composite->DrawTextA(Modes[i], rect.X, rect.Y, (int)Mode == i ? COLOR_RED : COLOR_WHITE);
		}
	}

	return 0;
}

static void ProcessActions(TActionClass* pAction, TriggerClass* pTrigger, int counter)
{
	if (pAction) {
		switch (Mode)
		{
		case ForceRun:
			pAction->Execute(HouseClass::FindByCountryIndex(pTrigger->Type->House->ArrayIndex), NULL, pTrigger, CellStruct{ 0,0 });
			break;
		case Enable:
		case Disable:
		default:
			break;
		}
		
		ProcessActions(pAction->NextAction, pTrigger, counter + 1);
	}
}

static void ProcessTriggers(TriggerClass* pTrigger, int counter)
{
	if (pTrigger) {
		switch (Mode)
		{
		case ForceRun:
			ProcessActions(pTrigger->Type->FirstAction, pTrigger, 0);
			TriggerExtMap[pTrigger].LastExecutedFrame = Unsorted::CurrentFrame;
			TriggerExtMap[pTrigger].ExecutedCount++;
			Message(L"Executed trigger <%s>", char2wchar(pTrigger->Type->Name));
			break;
		case Enable:
		{
			pTrigger->Enable();
			auto& ext = TriggerExtMap[pTrigger];
			if (ext.ResetTimer > -1)
			{
				pTrigger->Timer.TimeLeft = ext.ResetTimer;
				ext.ResetTimer = -1;
			}
			Message(L"Enabled trigger <%s>", char2wchar(pTrigger->Type->Name));
			break;
		}
		case Disable:
			pTrigger->Disable();
			Message(L"Disabled trigger <%s>", char2wchar(pTrigger->Type->Name));
			break;
		case Destroy:
			pTrigger->Destroy();
			Message(L"Destroyed trigger <%s>", char2wchar(pTrigger->Type->Name));
			break;
		case ChangeTimer:
			if (pTrigger->Enabled)
			{
				int elapsed = pTrigger->Timer.TimeLeft - pTrigger->Timer.GetTimeLeft();
				pTrigger->Timer.TimeLeft = ChangedTimer + elapsed;
			}
			else
			{
				pTrigger->Timer.TimeLeft = ChangedTimer;
				TriggerExtMap[pTrigger].ResetTimer = ChangedTimer;
			}
			Message(L"Set trigger <%s> timer to %d", char2wchar(pTrigger->Type->Name), ChangedTimer);
			break;
		default:
			break;
		}
		//ProcessTriggers(pTrigger->NextTrigger, counter + 1);
	}
}

static void GetEventCount(TEventClass* pEvent, int& count)
{
	if (pEvent)
	{
		count++;
		GetEventCount(pEvent->NextEvent, count);
	}
}

DEFINE_HOOK(69300B, ScrollClass_MouseUpdate_SkipMouseActionUpdate, 6)
{
	if (!bTriggerDebug)
		return 0;

	enum { SkipGameCode = 0x69301A };
	const Point2D mousePosition = WWMouseClass::Instance->XY1;

	auto isInRect = [&](const RectangleStruct& rect)
		{
			return rect.X <= mousePosition.X && mousePosition.X <= rect.X + rect.Width &&
				rect.Y <= mousePosition.Y && mousePosition.Y <= rect.Y + rect.Height;
		};

	HoveredTriggerIndex = -100;
	ModeIndex = -1;
	for (int i = 0; i < PageTriggerCount; ++i)
	{
		if (isInRect(TriggerDebugRect[i]))
		{
			HoveredTriggerIndex = i + CurrentPage * PageTriggerCount;
			R->Stack(STACK_OFFS(0x30, -0x24), 0);
			R->EAX(Action::None);
			return SkipGameCode;
		}
	}
	if (isInRect(TriggerDebugPageUp))
	{
		HoveredTriggerIndex = -1;
		R->Stack(STACK_OFFS(0x30, -0x24), 0);
		R->EAX(Action::None);
		return SkipGameCode;
	}
	if (isInRect(TriggerDebugPageDown))
	{
		HoveredTriggerIndex = -2;
		R->Stack(STACK_OFFS(0x30, -0x24), 0);
		R->EAX(Action::None);
		return SkipGameCode;
	}
	if (isInRect(TriggerDebugDetailed))
	{
		HoveredTriggerIndex = -3;
		R->Stack(STACK_OFFS(0x30, -0x24), 0);
		R->EAX(Action::None);
		return SkipGameCode;
	}
	if (isInRect(TriggerDebugSort))
	{
		HoveredTriggerIndex = -4;
		R->Stack(STACK_OFFS(0x30, -0x24), 0);
		R->EAX(Action::None);
		return SkipGameCode;
	}
	if (isInRect(TriggerDebugSearch))
	{
		HoveredTriggerIndex = -5;
		R->Stack(STACK_OFFS(0x30, -0x24), 0);
		R->EAX(Action::None);
		return SkipGameCode;
	}
	if (isInRect(TriggerDebugEnableModified))
	{
		HoveredTriggerIndex = -6;
		R->Stack(STACK_OFFS(0x30, -0x24), 0);
		R->EAX(Action::None);
		return SkipGameCode;
	}
	for (int i = 0; i < Count; ++i)
	{
		if (isInRect(TriggerDebugMode[i]))
		{
			ModeIndex = i;
			R->Stack(STACK_OFFS(0x30, -0x24), 0);
			R->EAX(Action::None);
			return SkipGameCode;
		}
	}
	return 0;
}

DEFINE_HOOK(6931A5, ScrollClass_WindowsProcedure_PressLeftMouseButton, 6)
{
	enum { SkipGameCode = 0x6931B4 };

	if (!bTriggerDebug)
		return 0;

	if (HoveredTriggerIndex >= 0)
	{
		if (HoveredTriggerIndex < SortedAllTriggers.size())
		{
			auto& obj = SortedAllTriggers[HoveredTriggerIndex];
			if (std::holds_alternative<TriggerClass**>(obj.item))
			{
				ProcessTriggers(*std::get<TriggerClass**>(obj.item), 0);
			}
			else
			{
				auto& ext = *std::get<TriggerClassExt*>(obj.item);
				if (ext.Type)
				{		
					switch (Mode)
					{
					case ForceRun:
					{
						auto newTrigger = TriggerClass::GetInstance(ext.Type);
						ProcessTriggers(newTrigger, 0);
						newTrigger->Destroy();
						break;
					}
					// new trigger does not allocate tag 
					case Enable:
					case Disable:
					case Destroy:
					case ChangeTimer:
					default:
						break;
					}
				}
			}
		}
	}
	else if (HoveredTriggerIndex > -100)
	{
		switch (HoveredTriggerIndex)
		{
		case -1:
		{
			if (CurrentPage > 0)
			{
				CurrentPage--;
			}
			break;
		}
		case -2:
		{
			if (!bTriggerDebugPageEnd)
			{
				CurrentPage++;
			}
			break;
		}
		case -3:
		{
			bTriggerDebugDetailed = !bTriggerDebugDetailed;
			break;
		}
		case -4:
		{
			Sort = TriggerSort(Sort+1);
			if (Sort == end)
				Sort = Raw;
			break;
		}
		case -5:
		{
			if (!MessageListClass::Instance->HasEditFocus())
			{
				MessageListClass::Instance->RemoveEdit();
				MessageListClass::Instance->AddEdit(0, TextPrintType::BrightColor, L"");
				bTriggerDebugEdited = true;
			}
			break;
		}
		case -6:
		{
			for (int i = 0; i < TriggerClass::Array->Count; i++) {
				auto pTrigger = TriggerClass::Array->GetItem(i);
				auto& ext = TriggerExtMap[pTrigger];
				if (ext.ResetTimer > -1)
				{
					pTrigger->Enable();
					pTrigger->Timer.TimeLeft = ext.ResetTimer;
					ext.ResetTimer = -1;
				}
			}
			Message(L"Enabled all triggers with modified timer");
			break;
		}
		default:
			break;
		}
		bPressedInButtonsLayer = true;
		R->Stack(STACK_OFFS(0x28, 0x8), 0);
		R->EAX(Action::None);
	}
	else if (ModeIndex > -1)
	{
		Mode = CurrentMode(ModeIndex);
		bPressedInButtonsLayer = true;
		if (Mode == ChangeTimer && !MessageListClass::Instance->HasEditFocus())
		{
			MessageListClass::Instance->RemoveEdit();
			MessageListClass::Instance->AddEdit(0, TextPrintType::BrightColor, L"");
			bTriggerDebugTimerEdited = true;
		}
		R->Stack(STACK_OFFS(0x28, 0x8), 0);
		R->EAX(Action::None);
	}

	return SkipGameCode;
}

DEFINE_HOOK(693268, ScrollClass_WindowsProcedure_ReleaseLeftMouseButton, 5)
{
	enum { SkipGameCode = 0x693276 };

	if (bPressedInButtonsLayer)
	{
		bPressedInButtonsLayer = false;
		R->Stack(STACK_OFFS(0x28, 0x8), 0);
		return SkipGameCode;
	}

	return 0;
}

DEFINE_HOOK(692F85, ScrollClass_MouseUpdate_SkipMouseLongPress, 7)
{
	enum { CheckMousePress = 0x692F8E, CheckMouseNoPress = 0x692FDC };

	GET(ScrollClass*, pThis, EBX);

	// 555A: AnyMouseButtonDown
	return (pThis->unknown_byte_554A && !bPressedInButtonsLayer) ? CheckMousePress : CheckMouseNoPress;
}

DEFINE_HOOK(6851F0, Logic_Init, 5)
{
	CurrentPage = 0;
	TriggerExtMap.clear();
	SortedTriggerArray.clear();
	DestroyedTriggers.clear();
	return 0;
}

DEFINE_HOOK(7265D1, TriggerClass_FireActions, 5)
{
	GET(TriggerClass*, pTrigger, EDI);
	TriggerExtMap[pTrigger].LastExecutedFrame = Unsorted::CurrentFrame;
	TriggerExtMap[pTrigger].ExecutedCount++;

	if (bTriggerExecuteLog && pTrigger->Type)
	{
		Log("[Trigger Execute] Frame: %d, Trigger: %s (%s)\n",
			Unsorted::CurrentFrame, pTrigger->Type->get_ID(), pTrigger->Type->Name);
	}

	return 0;
}

DEFINE_HOOK(7264C0, TriggerClass_RegisterEvent_Clear, 7)
{
	GET(TriggerClass*, pThis, ECX);
	TriggerExtMap[pThis].OccuredEvents.clear();
	
	int count = 0;
	GetEventCount(pThis->Type->FirstEvent, count);
	for (int i = 0; i < count; ++i)
	{
		TriggerExtMap[pThis].OccuredEvents.push_back(false);
	}
	return 0;
}

DEFINE_HOOK(726564, TriggerClass_RegisterEvent_Record, 6)
{
	GET(TriggerClass*, pThis, ESI);
	TriggerExtMap[pThis].OccuredEvents[(int)log2(R->EBP())] = true;
	return 0;
}

DEFINE_HOOK(726720, TriggerClass_Destroy, 7)
{
	GET(TriggerClass*, pThis, ECX);
	auto& destroyed = DestroyedTriggers.emplace_back(TriggerExtMap[pThis]);

	destroyed.Destroyed = true;
	destroyed.DestroyedFrame = Unsorted::CurrentFrame;
	destroyed.Type = pThis->Type;

	return 0;
}

