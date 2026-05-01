using System;
using System.Collections.Generic;
using System.IO;
using StarArk.Launcher.Models;
using Tomlyn;
using Tomlyn.Model;

namespace StarArk.Launcher.Services;

/// <summary>Scans mods/ next to launcher exe and parses each mod.toml.</summary>
public sealed class ModScanner
{
    public IReadOnlyList<ModInfo> Scan(string modsRoot)
    {
        var result = new List<ModInfo>();
        if (!Directory.Exists(modsRoot)) return result;

        foreach (var dir in Directory.EnumerateDirectories(modsRoot))
        {
            var tomlPath = Path.Combine(dir, "mod.toml");
            if (!File.Exists(tomlPath))
                continue;
            result.Add(ParseOne(dir, tomlPath));
        }
        return result;
    }

    private static ModInfo ParseOne(string folder, string tomlPath)
    {
        var issues = new List<ModIssue>();
        TomlTable? root = null;
        try
        {
            root = Toml.ToModel(File.ReadAllText(tomlPath));
        }
        catch (Exception ex)
        {
            issues.Add(new ModIssue { Severity = IssueSeverity.Error, Message = $"TOML parse failed: {ex.Message}" });
            return new ModInfo
            {
                Id = Path.GetFileName(folder),
                Type = "unknown",
                Version = "0.0.0",
                DisplayName = Path.GetFileName(folder) + " (broken)",
                FolderPath = folder,
                Issues = issues,
            };
        }

        string Req(string k, string fallback)
        {
            if (root!.TryGetValue(k, out var v) && v is string s && s.Length > 0) return s;
            issues.Add(new ModIssue { Severity = IssueSeverity.Error, Message = $"missing required field '{k}'" });
            return fallback;
        }
        string? Opt(string k) => root!.TryGetValue(k, out var v) && v is string s ? s : null;
        long?  OptL(string k) => root!.TryGetValue(k, out var v) && v is long l ? l : null;
        List<string> StrList(string k)
        {
            var list = new List<string>();
            if (root!.TryGetValue(k, out var v) && v is TomlArray a)
                foreach (var item in a)
                    if (item is string s) list.Add(s);
            return list;
        }

        var deps = new List<DependsOn>();
        if (root.TryGetValue("depends_on", out var depObj) && depObj is TomlArray depArr)
        {
            foreach (var item in depArr)
            {
                if (item is TomlTable t && t.TryGetValue("id", out var idV) && idV is string idS)
                {
                    deps.Add(new DependsOn
                    {
                        Id = idS,
                        Version = (t.TryGetValue("version", out var ver) && ver is string vs) ? vs : "*",
                    });
                }
            }
        }

        var schemaVersion = OptL("schema_version") ?? 0;
        if (schemaVersion != 1)
            issues.Add(new ModIssue { Severity = IssueSeverity.Warning, Message = $"schema_version={schemaVersion}, expected 1" });

        return new ModInfo
        {
            Id                 = Req("id", Path.GetFileName(folder)),
            Type               = Req("type", "unknown"),
            Version            = Req("version", "0.0.0"),
            DisplayName        = Req("display_name", Path.GetFileName(folder)),
            Description        = Opt("description"),
            Authors            = StrList("authors"),
            EngineMin          = Opt("engine_min") ?? "0.0.0",
            ScriptApiMin       = (int)(OptL("script_api_min") ?? 0),
            SupportedPipelines = StrList("supported_pipelines"),
            AppliesTo          = StrList("applies_to"),
            LoadAfter          = StrList("load_after"),
            LoadBefore         = StrList("load_before"),
            DependsOn          = deps,
            License            = Opt("license"),
            Homepage           = Opt("homepage"),
            FolderPath         = folder,
            Issues             = issues,
        };
    }
}
