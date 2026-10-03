namespace BOTWM.Server.DTO;

public class ItemParam
{
    public string Key { get; set; } = "";
    public byte Type { get; set; }
    public string Value { get; set; } = "";
}

public class SharedItem
{
    public string Id { get; set; } = "";
    public byte Owner { get; set; }
    public uint Revision { get; set; }
    public bool Removed { get; set; }
    public string Name { get; set; } = "";
    public string Map { get; set; } = "";
    public string Section { get; set; } = "";
    public List<ItemParam> Params { get; set; } = new();
}
