namespace BOTWM.Server.DTO
{
    public class ClientDTO
    {
        public List<SharedItem> SharedItems { get; set; } = new();
        public WorldDTO WorldData;
        public ClientPlayerDTO PlayerData;
        public EnemyDTO EnemyData;
        public QuestsDTO QuestData;
    }
}
