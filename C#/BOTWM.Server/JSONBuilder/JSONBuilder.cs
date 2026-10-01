using BOTWM.Server.DataTypes;
using BOTWM.Server.DTO;
using Newtonsoft.Json;
using System.Security.Cryptography;
using System.Text;

namespace BOTWM.Server.JSONBuilder
{

    public enum MessageType
    {
        error,
        ping,
        connect,
        update,
        disconnect
    }

    public class JSONBuilder
    {
        byte[] Data;
        List<byte> ByteData;

        public ServerDTO BuildFromBytesTest(byte[] data)
        {
            Data = data;

            GetArray(2);

            string SerializedJson = JsonConvert.SerializeObject(GetJson(typeof(ServerDTO)));

            ServerDTO Object = JsonConvert.DeserializeObject<ServerDTO>(SerializedJson);
            Object.SharedItems = ReadItems();

            return Object;
        }

        public Tuple<MessageType, object> BuildFromBytes(byte[] data)
        {
            Data = data;

            MessageType messageType = (MessageType)(int)GetArray(1)[0];

            object Object = null;

            if (messageType == MessageType.connect)
            {
                string SerializedJson = JsonConvert.SerializeObject(GetJson(typeof(ConnectDTO)));

                Object = JsonConvert.DeserializeObject<ConnectDTO>(SerializedJson);
            }
            else if (messageType == MessageType.update)
            {
                string SerializedJson = JsonConvert.SerializeObject(GetJson(typeof(ClientDTO)));

                Object = JsonConvert.DeserializeObject<ClientDTO>(SerializedJson);
                ((ClientDTO)Object).SharedItems = ReadItems();
            }
            else if(messageType == MessageType.ping)
            {
                Object = Encoding.UTF8.GetString(Data).TrimEnd('\0');
            }
            else if(messageType == MessageType.disconnect)
            {
                Object = Encoding.UTF8.GetString(Data).TrimEnd('\0');
            }

            return new Tuple<MessageType, object>(messageType, Object);
        }

        public byte[] BuildArrayOfBytes(object original, bool debug = false)
        {
            ByteData = new List<byte>();

            GetByteData(original);

            if (original is ServerDTO response) {
                // Leave older readers' legacy fields unchanged. New clients opt in
                // with a bounded UTF-8 JSON tail carrying values, never guest pointers.
                var json = Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(response.SharedItems));
                if (json.Length > 2048 || ByteData.Count + 4 + json.Length > 7168)
                    throw new InvalidDataException("Shared item packet exceeds the frame budget");
                ByteData.AddRange(new byte[] { 0x48, 0x31 });
                ByteData.AddRange(BitConverter.GetBytes((ushort)json.Length));
                ByteData.AddRange(json);
            }

            if(!debug)
                ByteData.InsertRange(0, BitConverter.GetBytes((short)ByteData.Count));

            return ByteData.ToArray();
        }

        private List<SharedItem> ReadItems()
        {
            if (Data.Length < 2 || Data[0] != 0x48 || Data[1] != 0x31) return new();
            if (Data.Length < 4) throw new InvalidDataException("Truncated item header");
            int length = BitConverter.ToUInt16(Data, 2);
            if (length > 2048 || length > Data.Length - 4) throw new InvalidDataException("Truncated item payload");
            var items = JsonConvert.DeserializeObject<List<SharedItem>>(Encoding.UTF8.GetString(Data, 4, length));
            if (items == null || items.Count > 2) throw new InvalidDataException("Invalid item batch");
            return items;
        }

        private object GetJson(Type original)
        {
            object value = null;

            if (original == typeof(int))
                value = BitConverter.ToInt32(GetArray(4), 0);
            else if (original == typeof(float))
                value = BitConverter.ToSingle(GetArray(4), 0);
            else if (original == typeof(bool))
                value = GetArray(1)[0] == 0x0 ? false : true;
            else if (original == typeof(byte))
                value = GetArray(1)[0];
            else if (original == typeof(short))
                value = BitConverter.ToInt16(GetArray(2), 0);
            else if (original == typeof(string))
            {
                int stringSize = GetArray(1)[0];

                if (stringSize == 0)
                    value = "";
                else
                    value = Encoding.UTF8.GetString(GetArray(stringSize));
            }
            else if (original == typeof(Vec3f))
            {
                Vec3f result = new Vec3f();

                result.x = BitConverter.ToSingle(GetArray(4), 0);
                result.y = BitConverter.ToSingle(GetArray(4), 0);
                result.z = BitConverter.ToSingle(GetArray(4), 0);

                value = result;

            }
            else if(original == typeof(Quaternion))
            {
                Quaternion result = new Quaternion();

                result.q1 = BitConverter.ToSingle(GetArray(4), 0);
                result.q2 = BitConverter.ToSingle(GetArray(4), 0);
                result.q3 = BitConverter.ToSingle(GetArray(4), 0);
                result.q4 = BitConverter.ToSingle(GetArray(4), 0);

                value = result;
            }
            else if (original == typeof(CharacterLocation))
            {

                CharacterLocation result = new CharacterLocation();

                result.Map = GetArray(1)[0];
                result.Section = GetArray(1)[0];

                value = result;

            }
            else if (original == typeof(CharacterEquipment))
            {

                CharacterEquipment result = new CharacterEquipment();

                result.WType = GetArray(1)[0];
                result.Sword = BitConverter.ToInt16(GetArray(2), 0);
                result.Shield = BitConverter.ToInt16(GetArray(2), 0);
                result.Bow = BitConverter.ToInt16(GetArray(2), 0);
                result.Head = BitConverter.ToInt16(GetArray(2), 0);
                result.Upper = BitConverter.ToInt16(GetArray(2), 0);
                result.Lower = BitConverter.ToInt16(GetArray(2), 0);

                value = result;

            }
            else if (original.IsGenericType && typeof(System.Collections.IList).IsAssignableFrom(original))
            {

                int ListSize = GetArray(1)[0];

                List<object> result = new List<object>();

                for (int i = 0; i < ListSize; i++)
                {

                    result.Add(GetJson(original.GetGenericArguments()[0]));

                }

                value = result;
            }
            else if (original == typeof(Dictionary<byte, ModelDataDTO>))
            {
                int DictionarySize = GetArray(1)[0];

                Dictionary<object, object> result = new Dictionary<object, object>();

                for (int i = 0; i < DictionarySize; i++)
                {
                    byte Key = GetArray(1)[0];

                    ModelDataDTO modelData = new ModelDataDTO();

                    modelData.ModelType = GetArray(1)[0];

                    if (modelData.ModelType < 2)
                    {
                        byte stringSize = GetArray(1)[0];
                        string model = Encoding.UTF8.GetString(GetArray(stringSize));
                        modelData.Model = model;
                        modelData.Mii = new BumiiDTO();
                    }
                    else
                    {
                        modelData.Model = "";
                        string sBumii = JsonConvert.SerializeObject(GetJson(typeof(BumiiDTO)));
                        modelData.Mii = JsonConvert.DeserializeObject<BumiiDTO>(sBumii);
                    }

                    result.Add(Key, modelData);
                }

                value = result;
            }
            else if (original.IsGenericType && typeof(System.Collections.IDictionary).IsAssignableFrom(original))
            {
                int DictionarySize = GetArray(1)[0];

                Dictionary<object, object> result = new Dictionary<object, object>();

                for (int i = 0; i < DictionarySize; i++)
                {
                    object Key = GetJson(original.GetGenericArguments()[0]);
                    object Value = GetJson(original.GetGenericArguments()[1]);

                    result.Add(Key, Value);
                }

                value = result;
            }
            else if (original == typeof(ConnectDTO))
            {
                ConnectDTO result = new ConnectDTO();

                int stringSize = GetArray(1)[0];
                result.Name = Encoding.UTF8.GetString(GetArray(stringSize));

                stringSize = GetArray(1)[0];
                result.Password = Encoding.UTF8.GetString(GetArray(stringSize));

                result.ModelData = new ModelDataDTO();

                stringSize = GetArray(1)[0];
                result.ModelData.ModelType = byte.Parse(Encoding.UTF8.GetString(GetArray(stringSize)));

                stringSize = BitConverter.ToInt16(GetArray(2), 0);
                string model = Encoding.UTF8.GetString(GetArray(stringSize));

                if(result.ModelData.ModelType < 2)
                {
                    result.ModelData.Model = model;
                    result.ModelData.Mii = new BumiiDTO();
                }
                else
                {
                    result.ModelData.Model = "";
                    result.ModelData.Mii = JsonConvert.DeserializeObject<BumiiDTO>(model);
                }

                value = result;
            }
            else
            {

                Dictionary<string, object> result = new Dictionary<string, object>();

                foreach (var item in original.GetFields())
                {

                    if (item.Name == "Schedule")
                        continue;

                    var res = GetJson(item.FieldType);

                    result.Add(item.Name, res);

                }

                value = result;
            }

            return value;
        }

        private void GetByteData(object original)
        {
            if (original.GetType() == typeof(int))
                AddBytes(BitConverter.GetBytes((int)original));
            else if (original.GetType() == typeof(float))
                AddBytes(BitConverter.GetBytes((float)original));
            else if (original.GetType() == typeof(bool))
                ByteData.Add((bool)original ? (byte)1 : (byte)0);
            else if (original.GetType() == typeof(byte))
                ByteData.Add((byte)original);
            else if (original.GetType() == typeof(short))
                AddBytes(BitConverter.GetBytes((short)original));
            else if (original.GetType() == typeof(string))
            { 
                ByteData.Add((byte)((string)original).Length);
                AddBytes(Encoding.UTF8.GetBytes((string)original), false);
            }
            else if(original.GetType() == typeof(ConnectDTO))
            {
                ConnectDTO origDTO = (ConnectDTO)original;

                ByteData.Add((byte)((string)origDTO.Name).Length);
                AddBytes(Encoding.UTF8.GetBytes((string)origDTO.Name), false);

                ByteData.Add((byte)((string)origDTO.Password).Length);
                AddBytes(Encoding.UTF8.GetBytes((string)origDTO.Password), false);

                string modelType = origDTO.ModelData.ModelType.ToString();

                ByteData.Add((byte)modelType.Length);
                AddBytes(Encoding.UTF8.GetBytes(modelType), false);

                if (origDTO.ModelData.ModelType < 2)
                { 
                    AddBytes(BitConverter.GetBytes((short)origDTO.ModelData.Model.Length));
                    AddBytes(Encoding.UTF8.GetBytes((string)origDTO.ModelData.Model), false);
                }
                else
                {
                    string MiiData = JsonConvert.SerializeObject(origDTO.ModelData.Mii);
                    AddBytes(BitConverter.GetBytes((short)MiiData.Length));
                    AddBytes(Encoding.UTF8.GetBytes(MiiData), false);
                }
            }
            else if(original.GetType() == typeof(ModelDataDTO))
            {
                ModelDataDTO dtoObj = (ModelDataDTO)original;
                ByteData.Add(dtoObj.ModelType);

                if(dtoObj.ModelType < 2)
                {
                    ByteData.Add((byte)(dtoObj.Model).Length);
                    AddBytes(Encoding.UTF8.GetBytes(dtoObj.Model), false);
                }
                else
                {
                    GetByteData(dtoObj.Mii);
                }
            }
            else if (original.GetType() == typeof(Vec3f))
            {
                Vec3f originalVec3f = (Vec3f)original;

                AddBytes(BitConverter.GetBytes(originalVec3f.x));
                AddBytes(BitConverter.GetBytes(originalVec3f.y));
                AddBytes(BitConverter.GetBytes(originalVec3f.z));
            }
            else if(original.GetType() == typeof(Quaternion))
            {
                Quaternion originalQuaternion = (Quaternion)original;

                AddBytes(BitConverter.GetBytes(originalQuaternion.q1));
                AddBytes(BitConverter.GetBytes(originalQuaternion.q2));
                AddBytes(BitConverter.GetBytes(originalQuaternion.q3));
                AddBytes(BitConverter.GetBytes(originalQuaternion.q4));
            }
            else if (original.GetType() == typeof(CharacterLocation))
            {
                CharacterLocation originalLocation = (CharacterLocation)original;

                ByteData.Add((byte)originalLocation.Map);
                ByteData.Add((byte)originalLocation.Section);
            }
            else if (original.GetType() == typeof(CharacterEquipment))
            {
                CharacterEquipment originalEquipment = (CharacterEquipment)original;

                //AddBytes(BitConverter.GetBytes(originalEquipment.WType));
                ByteData.Add((byte)originalEquipment.WType);
                AddBytes(BitConverter.GetBytes(originalEquipment.Sword));
                AddBytes(BitConverter.GetBytes(originalEquipment.Shield));
                AddBytes(BitConverter.GetBytes(originalEquipment.Bow));
                AddBytes(BitConverter.GetBytes(originalEquipment.Head));
                AddBytes(BitConverter.GetBytes(originalEquipment.Upper));
                AddBytes(BitConverter.GetBytes(originalEquipment.Lower));
            }
            else if (original.GetType().IsGenericType && typeof(System.Collections.IList).IsAssignableFrom(original.GetType()))
            {
                typeof(JSONBuilder).GetMethod("AddListData").MakeGenericMethod(original.GetType().GenericTypeArguments).Invoke(this, new[] { original });
            }
            else if (original.GetType().IsGenericType && typeof(System.Collections.IDictionary).IsAssignableFrom(original.GetType()))
            {
                typeof(JSONBuilder).GetMethod("AddDictData").MakeGenericMethod(original.GetType().GenericTypeArguments).Invoke(this, new[] { original });
            }
            else
            {
                foreach (var item in original.GetType().GetFields())
                    GetByteData(item.GetValue(original));
            }

        }

        private byte[] GetArray(int length, bool Reverse = true)
        {

            byte[] bytes = Data.Take(length).ToArray();

            Data = Data.Skip(length).ToArray();

            return bytes;

        }

        public void AddListData<T>(object original)
        {

            List<T> OriginalList = (List<T>)original;

            ByteData.Add((byte)OriginalList.Count);

            for (int i = 0; i < OriginalList.Count; i++)
            {
                GetByteData(OriginalList[i]);
            }

        }

        public void AddDictData<K, V>(object original)
        {
            Dictionary<K, V> OriginalDict = (Dictionary<K, V>)original;

            ByteData.Add((byte)OriginalDict.Count);

            for (int i = 0; i < OriginalDict.Count; i++)
            {
                GetByteData(OriginalDict.ElementAt(i).Key);
                GetByteData(OriginalDict.ElementAt(i).Value);
            }
        }

        private void AddBytes(byte[] bytes, bool Reverse = true)
        {
            ByteData.AddRange(bytes);
        }
    }
}
