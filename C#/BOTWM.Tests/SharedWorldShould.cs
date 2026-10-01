using BOTWM.Server.DTO;
using BOTWM.Server.ServerClasses;

namespace BOTWM.Tests;

public class SharedWorldShould
{
    [Fact]
    public async Task CombineIndependentHitsAgainstTheSameBaseline()
    {
        var enemies = new Enemy(2, true);
        void Report(int health) => enemies.Update(new EnemyDTO { Health = new() { new EnemyData(42, health) } });
        Report(100);
        await Task.WhenAll(Task.Run(() => Report(int.MinValue + 10)), Task.Run(() => Report(int.MinValue + 20)));
        Report(100); // Delayed baseline cannot heal combined damage.
        Report(int.MinValue); // Zero/invalid deltas are ignored.
        Report(int.MinValue + 1000001);
        foreach (var player in new[] { 0, 1 }) Assert.Equal(70, Assert.Single(enemies.GetQueue(player)).Health);
        Report(int.MinValue + 80);
        Assert.Equal(0, Assert.Single(enemies.GetQueue(0)).Health);
    }
    [Fact]
    public void KeepLowestReportedHealthAndIgnoreInvalidHealth()
    {
        var enemies = new Enemy(2, true);
        void Report(int hash, int health) => enemies.Update(new EnemyDTO {
            Health = new() { new EnemyData(hash, health) }
        });
        Report(42, 100);
        Report(42, 80);
        Report(42, 90); // A stale packet must not heal the enemy.
        Report(42, -1);
        Report(0, 0);
        foreach (int player in new[] { 0, 1 }) {
            var health = Assert.Single(enemies.GetQueue(player));
            Assert.Equal(42, health.Hash);
            Assert.Equal(80, health.Health);
            Assert.Empty(enemies.GetQueue(player));
        }
        enemies.FillQueue(1);
        Assert.Equal(80, Assert.Single(enemies.GetQueue(1)).Health);
    }

    [Fact]
    public async Task RetainSimultaneousQuestUpdatesAndDrainInBoundedBatches()
    {
        var quests = new Quests(2, true);
        await Task.WhenAll(Enumerable.Range(0, 250).Select(i => Task.Run(() =>
            quests.ProcessQuests(new List<string> { $"V{i}", $"V{i}" }))));
        Assert.Equal(250, quests.ServerQuests.Count);
        var first = quests.GetPlayerQuests(0);
        Assert.Equal(100, first.Count);
        first.AddRange(quests.GetPlayerQuests(0));
        first.AddRange(quests.GetPlayerQuests(0));
        Assert.Equal(250, first.Distinct().Count());
        Assert.Empty(quests.GetPlayerQuests(0));
        quests.FillQueue(0);
        Assert.Equal(250, quests.GetQuests(0).Count);
        Assert.Equal(250, quests.GetQuests(1).Count);
    }

    [Fact]
    public async Task RetainConcurrentEnemyDamageWithoutMutexTimeouts()
    {
        var enemies = new Enemy(2, true);
        await Task.WhenAll(Enumerable.Range(1, 200).Select(i => Task.Run(() =>
            enemies.Update(new EnemyDTO { Health = new() { new EnemyData(i, i) } }))));
        Assert.Equal(200, enemies.GetQueue(0).Count);
        Assert.Equal(200, enemies.GetQueue(1).Count);
    }

    [Fact]
    public void ClearDisabledServicesAndIgnoreExternalQuestUpdates()
    {
        var quests = new Quests(2, true);
        quests.ProcessQuests(new List<string> { "V1" });
        quests.UpdateServiceStatus(false);
        quests.Update(new QuestsDTO { Completed = new() { "V2" } });
        quests.ProcessQuests(new List<string> { "V3" });
        Assert.Empty(quests.ServerQuests);
        Assert.Empty(quests.GetQuests(0));
        var enemies = new Enemy(2, true);
        enemies.Update(new EnemyDTO { Health = new() { new EnemyData(42, 10) } });
        enemies.UpdateServiceStatus(false);
        enemies.Update(new EnemyDTO { Health = new() { new EnemyData(42, 1) } });
        Assert.Empty(enemies.EnemyList);
        Assert.Empty(enemies.GetQueue(1));
    }
    [Fact]
    public void DrainLargeEnemyBacklogsWithoutWrappingTheByteCount()
    {
        var enemies = new Enemy(2, true);
        enemies.Update(new EnemyDTO {
            Health = Enumerable.Range(1, 900).Select(i => new EnemyData(i, 10)).ToList()
        });
        var received = new List<EnemyData>();
        for (int i = 0; i < 5; ++i) {
            var batch = enemies.GetQueue(0);
            Assert.InRange(batch.Count, 1, 200);
            received.AddRange(batch);
        }
        Assert.Equal(900, received.Select(e => e.Hash).Distinct().Count());
        Assert.Empty(enemies.GetQueue(0));
    }

    [Fact]
    public void KeepEnemyAndQuestUpdatesWhenTheFrameHasNoRoom()
    {
        var enemies = new Enemy(2, true);
        enemies.Update(new EnemyDTO { Health = new() { new EnemyData(42, 10) } });
        Assert.Empty(enemies.GetQueue(0, 0));
        Assert.Equal(42, Assert.Single(enemies.GetQueue(0, 1)).Hash);
        var quests = new Quests(2, true);
        quests.ProcessQuests(new List<string> { "V1365", "V12" });
        Assert.Empty(quests.GetPlayerQuests(0, 5));
        Assert.Equal("V1365", Assert.Single(quests.GetPlayerQuests(0, 6)));
        Assert.Equal("V12", Assert.Single(quests.GetPlayerQuests(0, 4)));
    }

}
