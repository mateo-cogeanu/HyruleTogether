using BOTWM.Server.DTO;

namespace BOTWM.Server.ServerClasses
{
    public class Enemy
    {
        private readonly object EMutex = new();
        public bool isEnemySync;

        public Dictionary<int, int> EnemyList;
        public List<Dictionary<int, int>> Queue;

        public Enemy(int playerLimit, bool enemySync)
        {
            EnemyList = new Dictionary<int, int>();
            Queue = new List<Dictionary<int, int>>();
            for (int i = 0; i < playerLimit; i++)
                Queue.Add(new Dictionary<int, int>());
            UpdateServiceStatus(enemySync);
        }

        public void UpdateServiceStatus(bool newStatus)
        {
            lock (EMutex) isEnemySync = newStatus;
        }

        public void Update(EnemyDTO userData)
        {
            lock (EMutex)
            {
                // A timer must not discard baselines in an active combat session:
                // subsequent delta packets require the retained health state.
                if (!isEnemySync)
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
            // Negotiated clients encode local damage in a reserved negative
            // range. Server replies always contain ordinary absolute health.
            // This preserves both hits when peers damage the same baseline.
            long damage = (long)health - int.MinValue;
            if (hash != 0 && damage > 0 && damage <= 1000000 && EnemyList.TryGetValue(hash, out var current)) {
                health = Math.Max(0, current - (int)damage);
            }
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
