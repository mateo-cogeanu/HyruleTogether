using BOTWM.Server.DTO;
using Newtonsoft.Json;
using System.Text;
using System.Text.RegularExpressions;

namespace BOTWM.Server.ServerClasses;

// Session identities are never reused, including after a player slot reconnects.
// Tombstones stay in the session so a delayed spawn cannot resurrect a pickup.
public class SharedItems
{
    readonly object gate = new();
    readonly Dictionary<string, SharedItem> items = new();
    readonly Dictionary<string, long> creators = new();
    readonly List<Dictionary<string, SharedItem>> queues;
    readonly long[] sessions;
    long nextSession;
    public SharedItems(int players) {
        queues = Enumerable.Range(0, players).Select(_ => new Dictionary<string, SharedItem>()).ToList();
        sessions = new long[players];
    }
    public void Connect(int player) {
        lock (gate) { sessions[player] = ++nextSession; queues[player].Clear();
            foreach (var item in items.Values) queues[player][item.Id] = Clone(item); }
    }
    static SharedItem Clone(SharedItem item) => JsonConvert.DeserializeObject<SharedItem>(JsonConvert.SerializeObject(item))!;
    public void Update(int player, IEnumerable<SharedItem> updates) {
        lock (gate) foreach (var input in updates.Take(2)) {
            if (input == null || !ValidId(input.Id)) continue;
            if (items.TryGetValue(input.Id, out var existing)) {
                if (existing.Removed) { queues[player][input.Id] = Clone(existing); continue; }
                // Removal may come from the peer which picked up this item.
                if (!input.Removed && creators[input.Id] != sessions[player]) continue;
                if (input.Removed) existing.Removed = true;
                else if (input.Revision > existing.Revision) {
                    // Only the original session may move its item. A pose update
                    // cannot change the resource, durability, modifiers or scene.
                    if (!Valid(input) || !SameMetadata(existing, input)) continue;
                    existing.Params.Single(p => p.Key == "@M").Value =
                        input.Params.Single(p => p.Key == "@M").Value;
                    existing.Revision = input.Revision;
                }
                foreach (var q in queues) q[input.Id] = Clone(existing);
                continue;
            }
            // A local pickup may overtake its first spawn packet. Accept a
            // fully described early tombstone and acknowledge it, otherwise
            // that client's oldest unsent removal would block later drops.
            if (items.Count >= 4096 || !Valid(input)) continue;
            var added = Clone(input); added.Owner = (byte)player;
            items.Add(added.Id, added); creators.Add(added.Id, sessions[player]);
            foreach (var q in queues) q[added.Id] = Clone(added);
        }
    }
    public List<SharedItem> GetQueue(int player, int budget = 2048) {
        lock (gate) {
            var result = new List<SharedItem>();
            foreach (var pair in queues[player].ToArray()) {
                result.Add(Clone(pair.Value));
                if (result.Count > 2 || Encoding.UTF8.GetByteCount(JsonConvert.SerializeObject(result)) > budget) {
                    result.RemoveAt(result.Count - 1); break;
                }
                queues[player].Remove(pair.Key);
            }
            return result;
        }
    }
    static bool SameMetadata(SharedItem a, SharedItem b) =>
        a.Name == b.Name && a.Map == b.Map && a.Section == b.Section &&
        a.Params.Count == b.Params.Count && a.Params.All(p =>
            b.Params.Any(q => p.Key == q.Key && p.Type == q.Type &&
                (p.Key == "@M" || p.Value.Equals(q.Value, StringComparison.OrdinalIgnoreCase))));
    static bool ValidId(string id) => id != null && Regex.IsMatch(id, "^[a-zA-Z0-9-]{1,64}$");
    public static bool Valid(SharedItem item) {
        if (item.Name == null || !Regex.IsMatch(item.Name, "^(Item_|Weapon_|Obj_FireWoodBundle$)[A-Za-z0-9_]*$") || item.Name.Length > 80 ||
            item.Map == null || item.Map.Length == 0 || item.Map.Length > 32 || item.Section == null || item.Section.Length > 32 ||
            item.Params == null || item.Params.Count == 0 || item.Params.Count > 24) return false;
        int bytes = 0;
        var keys = new HashSet<string>();
        foreach (var p in item.Params) {
            if (p == null || !keys.Add(p.Key) || p.Key == null || !Allowed.TryGetValue(p.Key, out var type) || type != p.Type ||
                p.Value == null || !Regex.IsMatch(p.Value, "^[0-9a-fA-F]*$")) return false;
            int length = Sizes[type];
            if (p.Value.Length != length * 2) return false;
            if (type == 3 && p.Value != "00" && p.Value != "01") return false;
            if (type is 2 or 4 or 7) {
                var data = Convert.FromHexString(p.Value);
                for (int i = 0; i < data.Length; i += 4) {
                    Array.Reverse(data, i, 4);
                    float value = BitConverter.ToSingle(data, i);
                    if (!float.IsFinite(value) || Math.Abs(value) > 1000000) return false;
                }
            }
            bytes += 5 + length;
        }
        return bytes <= 192 && keys.Contains("@M") && keys.Contains("IsPlayerPut");
    }
    static readonly int[] Sizes = { 4, 4, 4, 1, 12, 0, 4, 48 };
    // Only verified value parameters may cross machines. Parent, callback and
    // other game pointers (@PC/@ND/@D) are intentionally absent.
    static readonly Dictionary<string, byte> Allowed = new() {
        ["IsPlayerPut"] = 3, ["AddParam"] = 0, ["AddSpecialFlag"] = 0,
        ["IsWeaponCreateByRawLife"] = 3, ["@RL"] = 0, ["@S"] = 4,
        ["Life"] = 0, ["@M"] = 7
    };
}
