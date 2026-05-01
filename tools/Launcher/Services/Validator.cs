using System.Collections.Generic;
using System.Linq;
using StarArk.Launcher.Models;

namespace StarArk.Launcher.Services;

/// <summary>
/// Validates a chosen game + addon combo against a target pipeline.
/// Records issues on each ModInfo's Issues list (mutates).
/// </summary>
public static class Validator
{
    public static IEnumerable<string> ValidateCombo(
        ModInfo game,
        IReadOnlyList<ModInfo> selectedAddons,
        string pipeline,
        string engineVersion,
        int    scriptApiVersion,
        IReadOnlyList<ModInfo> allMods)
    {
        var msgs = new List<string>();

        if (!game.SupportedPipelines.Contains(pipeline))
            msgs.Add($"游戏 '{game.Id}' 不支持 {pipeline} 管线");

        if (CompareVersion(engineVersion, game.EngineMin) < 0)
            msgs.Add($"引擎版本 {engineVersion} < 游戏要求 {game.EngineMin}");

        if (scriptApiVersion < game.ScriptApiMin)
            msgs.Add($"ScriptApi {scriptApiVersion} < 游戏要求 {game.ScriptApiMin}");

        var lookup = allMods.ToDictionary(m => m.Id);
        foreach (var dep in game.DependsOn)
            if (!lookup.ContainsKey(dep.Id))
                msgs.Add($"游戏 '{game.Id}' 依赖缺失: {dep.Id}");

        foreach (var addon in selectedAddons)
        {
            if (addon.AppliesTo.Count > 0 && !addon.AppliesTo.Contains(game.Id))
                msgs.Add($"附加 '{addon.Id}' 不适用于 '{game.Id}' (applies_to={string.Join(",", addon.AppliesTo)})");
            if (!addon.SupportedPipelines.Contains(pipeline))
                msgs.Add($"附加 '{addon.Id}' 不支持 {pipeline} 管线");
            if (CompareVersion(engineVersion, addon.EngineMin) < 0)
                msgs.Add($"引擎版本 {engineVersion} < 附加 '{addon.Id}' 要求 {addon.EngineMin}");
            if (scriptApiVersion < addon.ScriptApiMin)
                msgs.Add($"ScriptApi {scriptApiVersion} < 附加 '{addon.Id}' 要求 {addon.ScriptApiMin}");
            foreach (var dep in addon.DependsOn)
                if (!lookup.ContainsKey(dep.Id))
                    msgs.Add($"附加 '{addon.Id}' 依赖缺失: {dep.Id}");
        }

        return msgs;
    }

    /// <summary>Naive SemVer-ish compare: split by '.', compare numerically.</summary>
    private static int CompareVersion(string a, string b)
    {
        var pa = a.Split('.');
        var pb = b.Split('.');
        int n = System.Math.Max(pa.Length, pb.Length);
        for (int i = 0; i < n; i++)
        {
            int ai = i < pa.Length && int.TryParse(pa[i], out var x) ? x : 0;
            int bi = i < pb.Length && int.TryParse(pb[i], out var y) ? y : 0;
            if (ai != bi) return ai.CompareTo(bi);
        }
        return 0;
    }
}
