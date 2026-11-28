#include "Hooks.h"
#include "Lang.h"
#include "Menu.h"
#include "Precision.h"
#include "Settings.h"
#include "Trails.h"

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			Precision::Install();  // every plugin's DLL is loaded by now
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			Trails::Init();
			Lang::Set(Settings::Get().language);
			Menu::Register();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			Trails::Reset();
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	logs::info("Attack Trails loading");
	Settings::Load();
	Hooks::Install();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
