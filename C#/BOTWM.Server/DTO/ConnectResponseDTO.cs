namespace BOTWM.Server.DTO
{
    public class ConnectResponseDTO
    {
        public int Response;
        public string Reason = "";
        public int PlayerNumber;
        public ServerSettings Settings;
        public bool QuestSync;
        public string EnemySyncList;
    }
}
