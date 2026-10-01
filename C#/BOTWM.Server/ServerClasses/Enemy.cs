using BOTWM.Server.DTO;

namespace BOTWM.Server.ServerClasses
{
    public class Enemy
    {
        const int CLEARMINUTES = 60;

        private readonly object EMutex = new();
        public bool isEnemySync;
        private DateTime LastClear;

        public Dictionary<int, int> EnemyList;
        public List<Dictionary<int, int>> Queue;

        public Enemy(int playerLimit, bool enemySync)
        {
            EnemyList = new Dictionary<int, int>();
            Queue = new List<Dictionary<int, int>>();
            for (int i = 0; i < playerLimit; i++)
                Queue.Add(new Dictionary<int, int>());
            UpdateServiceStatus(enemySync);
            LastClear = DateTime.Now;
        }

        public void UpdateServiceStatus(bool newStatus)
        {
            lock (EMutex) isEnemySync = newStatus;
        }

        public void Update(EnemyDTO userData)
        {
            lock (EMutex)
            {
                double TimeSinceLastClear = DateTime.Now.Subtract(LastClear).TotalMinutes;
                if (!isEnemySync || TimeSinceLastClear > CLEARMINUTES)
                    ClearEnemyData();
                if (!isEnemySync) return;

                foreach (EnemyData Enemy in userData.Health)
                    UpdateEnemyHealth(Enemy.Hash, Enemy.Health);

            }
        }

        public void ClearEnemyData()
        {
            lock (EMutex)
            {

                EnemyList.Clear();
                for (int i = 0; i < Queue.Count; i++)
                    Queue[i].Clear();

                LastClear = DateTime.Now;

            }

            return;
        }

        public void FillQueue(int playerNumber)
        {
            lock (EMutex)
            {

                Queue[playerNumber].Clear();

                foreach (KeyValuePair<int, int> kvp in EnemyList)
                    Queue[playerNumber].Add(kvp.Key, kvp.Value);

            }
        }

        public List<EnemyData> GetQueue(int playerNumber, int limit = 200)
        {
            List<EnemyData> Data = new List<EnemyData>();

            lock (EMutex)
            {

                foreach (KeyValuePair<int, int> kvp in Queue[playerNumber].Take(Math.Clamp(limit, 0, 200)).ToArray())
                {
                    Data.Add(new EnemyData(kvp.Key, kvp.Value));
                    Queue[playerNumber].Remove(kvp.Key);
                }

            }

            return Data;
        }

        private void UpdateEnemyHealth(int hash, int health)
        {
            if (hash == 0 || health < 0) return;

            if (!EnemyList.ContainsKey(hash) || (EnemyList.ContainsKey(hash) && EnemyList[hash] > health))
            {
                EnemyList[hash] = health;

                for (int i = 0; i < Queue.Count; i++)
                    Queue[i][hash] = health;
            }
        }
    }
}
