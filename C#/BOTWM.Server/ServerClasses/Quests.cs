using BOTWM.Server.DTO;

namespace BOTWM.Server.ServerClasses
{
    public class Quests
    {
        private readonly object QMutex = new();

        public bool isQuestSync;

        public List<string> ServerQuests;
        public List<List<string>> Queue = new List<List<string>>();

        public Quests(int playerLimit, bool questSync)
        {
            ServerQuests = new List<string>();

            for (int i = 0; i < playerLimit; i++)
                Queue.Add(new List<string>());

            UpdateServiceStatus(questSync);
        }

        public void UpdateServiceStatus(bool newStatus)
        {
            lock (QMutex) isQuestSync = newStatus;
        }

        public void Update(QuestsDTO userData)
        {

            lock (QMutex)
            {
                if (!isQuestSync) { ClearQuests(); return; }
                ProcessQuests(userData);
            }
        }

        public void ProcessQuests(QuestsDTO userData) => ProcessQuests(userData.Completed);

        public void ProcessQuests(List<string> quests)
        {
            lock (QMutex)
            {
                if (!isQuestSync) return;
                foreach (string quest in quests)
                {
                    if (string.IsNullOrEmpty(quest) || ServerQuests.Contains(quest)) continue;
                    ServerQuests.Add(quest);
                    foreach (var queue in Queue) queue.Add(quest);
                }
            }
        }

        public void ClearQuests()
        {
            lock (QMutex)
            {

                ServerQuests.Clear();

                for (int i = 0; i < Queue.Count; i++)
                {
                    Queue[i].Clear();
                }

            }
        }

        public void FillQueue(int playerNumber)
        {
            lock (QMutex)
            {

                Queue[playerNumber].Clear();

                foreach (string Quest in ServerQuests)
                    Queue[playerNumber].Add(Quest);

            }
        }

        public List<string> GetQuests(int playerNumber)
        {
            lock (QMutex)
            {

                List<string> QuestData = new List<string>(Queue[playerNumber]);

                Queue[playerNumber].Clear();

                return QuestData;
            }
        }

        public List<string> GetPlayerQuests(int playerNumber, int byteBudget = int.MaxValue)
        {
            List<string> PlayerQuests = new List<string>();

            lock (QMutex)
            {

                for (int i = 0; i < 100; i++)
                {
                    if (Queue[playerNumber].Count == 0)
                        break;

                    int size = 1 + System.Text.Encoding.UTF8.GetByteCount(Queue[playerNumber][0]);
                    if (size > byteBudget) break;
                    byteBudget -= size;
                    PlayerQuests.Add(Queue[playerNumber][0]);
                    Queue[playerNumber].RemoveAt(0);
                }

            }

            return PlayerQuests;
        }
    }
}
