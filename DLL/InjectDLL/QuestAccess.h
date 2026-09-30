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

                for (auto const& pair : QuestSyncer->numberOfQuests)
                {
                    std::string QType = pair.first;
                    int QNumber = pair.second;

                    for (int i = 0; i < QNumber; i++)
                    {
                        QuestSyncer->QuestList[QType + std::to_string(i)].Value = 0;
                        QuestSyncer->QuestList[QType + std::to_string(i)].beingChanged = false;
                    }
                }

                QuestSyncer->changedQuests.clear();

            }
		}
	};
}
