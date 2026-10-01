using BOTWM.Server.DTO;
using BOTWM.Server.ServerClasses;
using BOTWM.Server.JSONBuilder;

namespace BOTWM.Tests;
public class SharedItemsShould
{
    static SharedItem Drop(string id = "test-1") => new() {
        Id = id, Name = "Weapon_Spear_030", Map = "MainField", Section = "D-6",
        Params = new() {
            new() { Key = "IsPlayerPut", Type = 3, Value = "00" },
            new() { Key = "Life", Type = 0, Value = "000007d0" },
            new() { Key = "@M", Type = 7, Value = string.Concat(Enumerable.Repeat("00000000", 12)) }
        }
    };
    [Fact] public void ReplicatePreservedMetadataAndTombstoneDelayedCreates() {
        var service = new SharedItems(2); service.Connect(0); service.Connect(1);
        service.Update(0, new[] { Drop() });
        Assert.Equal("000007d0", Assert.Single(service.GetQueue(1)).Params[1].Value);
        service.Update(1, new[] { new SharedItem { Id = "test-1", Removed = true } });
        Assert.True(Assert.Single(service.GetQueue(0)).Removed);
        service.Update(0, new[] { Drop() });
        Assert.True(Assert.Single(service.GetQueue(0)).Removed);
        service.Connect(1); Assert.True(Assert.Single(service.GetQueue(1)).Removed);
    }
    [Fact] public void RejectPointersMalformedParamsAndIdentityHijacking() {
        var service = new SharedItems(2); service.Connect(0); service.Connect(1);
        var invalid = Drop(); invalid.Params.Add(new() { Key = "@D", Type = 6, Value = "12345678" });
        service.Update(0, new[] { invalid }); Assert.Empty(service.GetQueue(1));
        invalid = Drop(); invalid.Params[2].Value = "7f800000" + new string('0', 88);
        service.Update(0, new[] { invalid }); Assert.Empty(service.GetQueue(1));
        service.Update(0, new[] { Drop() }); service.GetQueue(0); service.GetQueue(1);
        var hijack = Drop(); hijack.Name = "Item_Material_04";
        service.Update(1, new[] { hijack }); Assert.Empty(service.GetQueue(0));
        service.Connect(0); service.GetQueue(0);
        service.Update(0, new[] { hijack }); Assert.Empty(service.GetQueue(1));
    }
    [Fact] public void RetainItemsUntilPacketBudgetFits() {
        var service = new SharedItems(2); service.Connect(0);
        service.Update(0, new[] { Drop("a"), Drop("b") });
        Assert.Empty(service.GetQueue(1, 2));
        Assert.Equal(2, service.GetQueue(1).Count); Assert.Empty(service.GetQueue(1));
    }
    [Fact] public void PreserveOptionalTailThroughServerBinaryProtocol() {
        var dto = new ServerDTO { SharedItems = new() { Drop() } };
        dto.EnemyData.Health = new(); dto.QuestData.Completed = new();
        dto.NameData.Names = new(); dto.ModelData.Models = new();
        dto.TeleportData.Destination = new(); dto.PropHuntData.StartingPosition = new();
        var bytes = new JSONBuilder().BuildArrayOfBytes(dto);
        var parsed = new JSONBuilder().BuildFromBytesTest(bytes);
        Assert.Equal("000007d0", Assert.Single(parsed.SharedItems).Params[1].Value);
        Assert.Equal("MainField", parsed.SharedItems[0].Map);
    }
    [Fact] public void AcknowledgeAPickupWhichOvertakesItsInitialSpawnPacket() {
        var service = new SharedItems(2); service.Connect(0); service.Connect(1);
        var picked = Drop(); picked.Removed = true;
        service.Update(0, new[] { picked });
        Assert.True(Assert.Single(service.GetQueue(0)).Removed);
        service.Update(0, new[] { Drop() });
        Assert.True(Assert.Single(service.GetQueue(0)).Removed);
        Assert.True(Assert.Single(service.GetQueue(1)).Removed);
    }

}
