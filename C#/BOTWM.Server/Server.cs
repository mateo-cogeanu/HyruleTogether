using BOTWM.Server.DTO;
using BOTWM.Server.HelperTypes;
using BOTWM.Server.ServerClasses;
using Newtonsoft.Json;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using BOTWM.Server.JSONBuilder;
using System.Diagnostics;
using BOTW.Logging;

namespace BOTWM.Server
{
    public class Server
    {
        bool serverOpen = false;

        public string Version = "0.20.0";

        public short SerializationRate = 60;
        public short TargetFPS = 60;
        public short SleepMultiplier = 1;
        public bool isLocalTest = false;
        public bool ischaracterSpawn = true;
        public bool DisplayNames = true;
        public short GlyphDistance = 250;
        public short GlyphTime = 60;
        public bool isQuestSync = false;
        public bool isEnemySync = false;
        public string Gamemode = "";

        public bool EnemyLog { get; set; }

        public int ClientLog { get; set; }
        public bool ServerLog { get; set; }

        Socket? listen;
        Thread? listenThread;
        List<Thread> clientThreads = new List<Thread>();
        HashSet<Socket> clientSockets = new HashSet<Socket>();
        readonly object clientThreadsLock = new object();

        public bool serverStart(string ip, int port, string password, string description, ServerSettings settings)
        {

            this.Gamemode = settings.SettingsName;

            try
            {
                IPAddress bindAddress;
                if (ip.Equals("localhost", StringComparison.OrdinalIgnoreCase) || ip == "0.0.0.0")
                    bindAddress = IPAddress.Any;
                else if (ip == "::")
                    bindAddress = IPAddress.IPv6Any;
                else if (!IPAddress.TryParse(ip, out bindAddress!))
                    bindAddress = Dns.GetHostAddresses(ip).First(address =>
                        address.AddressFamily is AddressFamily.InterNetwork or AddressFamily.InterNetworkV6);

                listen = new Socket(bindAddress.AddressFamily, SocketType.Stream, ProtocolType.Tcp);
                if (bindAddress.AddressFamily == AddressFamily.InterNetworkV6)
                    listen.DualMode = bindAddress.Equals(IPAddress.IPv6Any);
                listen.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
                listen.Bind(new IPEndPoint(bindAddress, port));
                listen.Listen(100);

                Logger.LogInformation($"Server listening on {listen.LocalEndPoint}.");
                ServerData.Startup(ip, port, password, description, settings);
                serverOpen = true;
                return true;
            }
            catch (Exception ex)
            {
                Logger.LogError("Could not open the server.", ex.Message);
                listen?.Dispose();
                listen = null;
                return false;
            }
        }

        public void stopServer()
        {
            serverOpen = false;
            listen?.Close();
            listenThread?.Join(TimeSpan.FromSeconds(2));
            Thread[] clients;
            lock (clientThreadsLock)
            {
                foreach (Socket socket in clientSockets.ToArray())
                    socket.Close();
                clients = clientThreads.ToArray();
            }
            foreach (Thread clientThread in clients)
                clientThread.Join(TimeSpan.FromSeconds(2));
        }

        public void startListen()
        {
            listenThread = new Thread(serverListen);
            listenThread.IsBackground = true;
            listenThread.Start();
        }

        public void serverListen()
        {
            while(serverOpen)
            {
                try
                {
                    Socket connection = listen!.Accept();
                    connection.NoDelay = true;
                    var clientThread = new Thread(() => handleClient(connection)) { IsBackground = true };
                    lock (clientThreadsLock)
                    {
                        clientSockets.Add(connection);
                        clientThreads.Add(clientThread);
                    }
                    clientThread.Start();
                }
                catch (SocketException) when (!serverOpen) { }
                catch (ObjectDisposedException) when (!serverOpen) { }
            }
        }

        public void handleClient(Socket connection)
        {
            bool ClientConnected = true;
            int PlayerNumber = -1;
            string PlayerName = "";
            
            while(serverOpen && ClientConnected)
            {
                try
                {
                    byte[] frame = ReceiveFrame(connection);
                    Tuple<MessageType, object> ClientMessage = new JSONBuilder.JSONBuilder().BuildFromBytes(frame);

                    if (ClientMessage.Item1 == MessageType.error)
                    {
                        throw new Exception($"[{PlayerName}] Error receiving message. Disconnecting player...");
                    }
                    else if(ClientMessage.Item1 == MessageType.ping)
                    {
                        var PingResult = new PingDTO();

                        if(ServerData.Configuration.PASSWORD != (string)ClientMessage.Item2)
                            PingResult = new PingDTO()
                            {
                                CorrectPassword = false,
                                Description = "",
                                PlayerList = new NamesDTO(),
                                GameMode = "",
                                PlayerLimit = 32
                            };
                        else
                            PingResult = new PingDTO()
                            {
                                CorrectPassword = true,
                                Description = ServerData.Configuration.DESCRIPTION,
                                PlayerList = ServerData.GetPlayers(),
                                GameMode = this.Gamemode,
                                PlayerLimit = 32
                            };

                        SendAll(connection, Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(PingResult)));

                        connection.Close();
                        ClientConnected = false;

                    }
                    else if(ClientMessage.Item1 == MessageType.connect)
                    {
                        ConnectDTO UserConfiguration = (ConnectDTO)ClientMessage.Item2;
                        ConnectResponseDTO AssignationResult = ServerData.TryAssigning(UserConfiguration);

                        if(AssignationResult.Response != 1)
                        {
                            SendAll(connection, Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(AssignationResult)));
                            connection.Close();
                            ClientConnected = false;
                            Logger.LogInformation($"Player {UserConfiguration.Name} tried to connect but failed with error {AssignationResult.Response}");
                            break;
                        }

                        PlayerNumber = AssignationResult.PlayerNumber;
                        PlayerName = ServerData.PlayerList[PlayerNumber].Name;
                        SendAll(connection, Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(AssignationResult)));

                        Logger.LogInformation($"Player {UserConfiguration.Name} joined the server. Assigned to player {AssignationResult.PlayerNumber + 1}.");
                    }
                    else if(ClientMessage.Item1 == MessageType.update)
                    {
                        ServerData.SetConnection(PlayerNumber, true);

                        ClientDTO UserInformation = (ClientDTO)ClientMessage.Item2;

                        ServerData.UpdateWorldData(UserInformation.WorldData, PlayerNumber);
                        ServerData.UpdatePlayerData(UserInformation.PlayerData, PlayerNumber);
                        ServerData.UpdateEnemyData(UserInformation.EnemyData);
                        ServerData.UpdateQuestData(UserInformation.QuestData);

                        ServerDTO serverDTO = ServerData.GetData(PlayerNumber);
                        serverDTO.NetworkData.Map(this);

                        SendAll(connection, new JSONBuilder.JSONBuilder().BuildArrayOfBytes(serverDTO));

                        ServerData.ClearDeathSwap(PlayerNumber);
                    }
                    else if(ClientMessage.Item1 == MessageType.disconnect)
                    {
                        Logger.LogInformation($"Player {ServerData.GetPlayer(PlayerNumber).Name} disconnected. {(string)ClientMessage.Item2}");
                        connection.Close();
                        ClientConnected = false;
                        ServerData.SetConnection(PlayerNumber, false);
                    }
                }
                catch (Exception ex)
                {
                    string identity = PlayerNumber >= 0 ? ServerData.GetPlayer(PlayerNumber).Name : connection.RemoteEndPoint?.ToString() ?? "unknown client";
                    Logger.LogInformation($"Player {identity} disconnected.", ex.Message);
                    Logger.LogDebug(ex.StackTrace);
                    if (PlayerNumber >= 0)
                        ServerData.SetConnection(PlayerNumber, false);
                    connection.Close();
                    ClientConnected = false;
                }
            }
            lock (clientThreadsLock)
            {
                clientSockets.Remove(connection);
                clientThreads.Remove(Thread.CurrentThread);
            }
        }

        private static byte[] ReceiveFrame(Socket connection)
        {
            byte[] type = ReceiveExactly(connection, 1);
            int length = type[0] == (byte)MessageType.ping ? 6144 : 7168;
            byte[] result = new byte[length];
            result[0] = type[0];
            byte[] remainder = ReceiveExactly(connection, length - 1);
            Buffer.BlockCopy(remainder, 0, result, 1, remainder.Length);
            return result;
        }

        private static byte[] ReceiveExactly(Socket connection, int length)
        {
            byte[] result = new byte[length];
            int offset = 0;
            while (offset < length)
            {
                int received = connection.Receive(result, offset, length - offset, SocketFlags.None);
                if (received == 0)
                    throw new EndOfStreamException("The client closed the connection.");
                offset += received;
            }
            return result;
        }

        private static void SendAll(Socket connection, byte[] data)
        {
            int offset = 0;
            while (offset < data.Length)
                offset += connection.Send(data, offset, data.Length - offset, SocketFlags.None);
        }
    }
}
