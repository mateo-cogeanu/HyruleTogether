#pragma once

namespace MemoryAccess
{
	class QuestAccess
	{
		std::vector<std::string> ServerSettings;
        bool IsQuestSync = false;
        bool eventStatus = false;
        bool questSyncUsingEvent = false;
        DWORD fixtureStarted = 0;
        bool fixtureApplied = false;

	public:
		Memory::Quests_class* QuestSyncer = new Memory::Quests_class();

		void Startup(bool isQuestSync, std::vector<std::string> questServerSettings)
		{
			std::lock_guard<std::recursive_mutex> guard(QuestSyncer->QuestMutex);
			this->ServerSettings = questServerSettings;
            this->IsQuestSync = isQuestSync;
		}

        DTO::QuestDTO* UpdateQuests(bool paused)
        {
            std::lock_guard<std::recursive_mutex> guard(QuestSyncer->QuestMutex);
            DTO::QuestDTO* result = new DTO::QuestDTO();

            if (IsQuestSync)
            {
                // Only the isolated test harness opts into a synthetic quest
                // change. This tests transport/application, not NPC dialogue.
                const char* fixtureName = std::getenv("HYRULE_TEST_QUEST_NAME");
                if (TestTelemetry::enabled() && fixtureName) {
                    const auto id = QuestSyncer->findQuest(fixtureName);
                    const auto entry = QuestSyncer->QuestList.find(id);
                    if (entry != QuestSyncer->QuestList.end()) {
                        const auto& quest = entry->second;
                        if (!paused && !fixtureStarted) fixtureStarted = GetTickCount();
                        if (!paused && !fixtureApplied && fixtureStarted &&
                            GetTickCount() - fixtureStarted >= 8000 &&
                            std::getenv("HYRULE_TEST_QUEST_SOURCE") && quest.Type == "V") {
                            const char *prerequisite = std::getenv("HYRULE_TEST_QUEST_PREREQUISITE");
                            if (prerequisite) {
                                const auto prerequisiteId = QuestSyncer->findQuest(prerequisite);
                                const auto flag = QuestSyncer->QuestList.find(prerequisiteId);
                                if (flag != QuestSyncer->QuestList.end()) {
                                    const auto before = Memory::read_bytes(flag->second.Address, 1, __FUNCTION__)[0];
                                    Memory::write_byte(flag->second.Address, before | 1, __FUNCTION__);
                                    TestTelemetry::emit("quest_prerequisite_source", -1, [&](auto &w) {
                                        w.Key("id"); w.String(prerequisiteId.c_str());
                                        w.Key("before"); w.Uint(before);
                                    }, true);
                                }
                            }
                            const auto value = Memory::read_bytes(quest.Address, 1, __FUNCTION__)[0];
                            Memory::write_byte(quest.Address, value | 1, __FUNCTION__);
                            fixtureApplied = true;
                            TestTelemetry::emit("quest_fixture_source", -1, [&](auto& json) {
                                json.Key("id"); json.String(id.c_str());
                                json.Key("before"); json.Uint(value);
                            }, true);
                        }
                        TestTelemetry::emit("quest_fixture_readback", -1, [&](auto& json) {
                            json.Key("id"); json.String(id.c_str());
                            json.Key("value"); json.Uint(Memory::read_bytes(quest.Address, 1, __FUNCTION__)[0]);
                        });
                    }
                }
                if (TestTelemetry::enabled()) {
                    const char *prerequisite = std::getenv("HYRULE_TEST_QUEST_PREREQUISITE");
                    if (prerequisite) {
                        const auto id = QuestSyncer->findQuest(prerequisite);
                        const auto flag = QuestSyncer->QuestList.find(id);
                        if (flag != QuestSyncer->QuestList.end())
                            TestTelemetry::emit("quest_prerequisite_readback", -1, [&](auto &w) {
                                w.Key("id"); w.String(id.c_str());
                                w.Key("value"); w.Uint(Memory::read_bytes(flag->second.Address, 1, __FUNCTION__)[0]);
                            });
                    }
                }
                QuestSyncer->readQuests();
                result->Completed = QuestSyncer->getChangedQuests();
            }

            return result;
        }

		void SetServerData(std::vector<std::string> questData, bool paused, bool questSyncReady)
		{
            std::lock_guard<std::recursive_mutex> guard(QuestSyncer->QuestMutex);
            if (IsQuestSync)
            {
                for (int i = 0; i < questData.size(); i++)
                    if (std::find(QuestSyncer->questsToChange.begin(), QuestSyncer->questsToChange.end(), questData[i]) == QuestSyncer->questsToChange.end())
                        QuestSyncer->questsToChange.push_back(questData[i]);

                if (questSyncReady && !paused) {
                    QuestSyncer->readQuests();
                    questSyncUsingEvent = QuestSyncer->updateQuests(eventStatus);
                }

                if (questSyncReady && !paused)
                {
                    QuestSyncer->resyncQuests();
                }

                eventStatus = questSyncUsingEvent;
            }
            else
            {


                QuestSyncer->serverQuests.clear();
                QuestSyncer->koroksToAdd = 0;
                QuestSyncer->boolsToChange.clear();
                QuestSyncer->itemsToAdd.clear();
                QuestSyncer->intsToChange.clear();

                // Reset indexed identities, including appended prerequisites.
                // Do not recreate catalogue entries absent from guest memory.
                for (auto& pair : QuestSyncer->QuestList) {
                    pair.second.Value = 0;
                    pair.second.beingChanged = false;
                    pair.second.changed = false;
                }

                QuestSyncer->changedQuests.clear();

            }
		}
	};
}
