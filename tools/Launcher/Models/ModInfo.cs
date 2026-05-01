using System.Collections.Generic;

namespace StarArk.Launcher.Models;

/// <summary>Parsed mod.toml — see ModSpec §2.</summary>
public sealed class ModInfo
{
    public required string Id { get; init; }
    public required string Type { get; init; }              // "game" | "addon"
    public required string Version { get; init; }
    public required string DisplayName { get; init; }
    public string? Description { get; init; }
    public List<string> Authors { get; init; } = new();
    public string EngineMin { get; init; } = "0.0.0";
    public int ScriptApiMin { get; init; } = 0;
    public List<string> SupportedPipelines { get; init; } = new();
    public List<string> AppliesTo { get; init; } = new();
    public List<string> LoadAfter { get; init; } = new();
    public List<string> LoadBefore { get; init; } = new();
    public List<DependsOn> DependsOn { get; init; } = new();
    public string? License { get; init; }
    public string? Homepage { get; init; }

    /// <summary>Absolute folder path of this mod.</summary>
    public required string FolderPath { get; init; }

    /// <summary>Issues found during parse / validate. Empty = healthy.</summary>
    public List<ModIssue> Issues { get; init; } = new();

    public bool IsHealthy => Issues.TrueForAll(i => i.Severity != IssueSeverity.Error);
    public bool IsGame    => Type == "game";
    public bool IsAddon   => Type == "addon";
}

public sealed class DependsOn
{
    public required string Id { get; init; }
    public string Version { get; init; } = "*";
}

public enum IssueSeverity { Info, Warning, Error }

public sealed class ModIssue
{
    public required IssueSeverity Severity { get; init; }
    public required string Message { get; init; }
}
