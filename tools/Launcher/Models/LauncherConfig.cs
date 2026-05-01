using System;
using System.IO;
using System.Text.Json;

namespace StarArk.Launcher.Models;

/// <summary>
/// launcher.config.json — sits next to the launcher executable.
/// Keeps runtime exe name, mods dir name, etc. configurable so different
/// games can rename the exe without rebuilding the launcher.
/// </summary>
public sealed class LauncherConfig
{
    public string RuntimeExecutable { get; set; } = "StarArkSamples.exe";
    public string ModsDirectory     { get; set; } = "mods";
    public string GameTitle         { get; set; } = "StarArk";
    public string DefaultPipeline   { get; set; } = "forward";
    public bool   AllowDeferred     { get; set; } = true;

    public static LauncherConfig LoadOrDefault(string baseDir)
    {
        var path = Path.Combine(baseDir, "launcher.config.json");
        if (!File.Exists(path))
            return new LauncherConfig();
        try
        {
            var json = File.ReadAllText(path);
            return JsonSerializer.Deserialize<LauncherConfig>(json,
                       new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
                   ?? new LauncherConfig();
        }
        catch (Exception)
        {
            return new LauncherConfig();
        }
    }
}
