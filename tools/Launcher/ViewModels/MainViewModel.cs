using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using StarArk.Launcher.Models;
using StarArk.Launcher.Services;

namespace StarArk.Launcher.ViewModels;

public sealed class ModItemVM : INotifyPropertyChanged
{
    public ModInfo Mod { get; }
    public ModItemVM(ModInfo mod) { Mod = mod; }

    public string Id          => Mod.Id;
    public string DisplayName => Mod.DisplayName;
    public string Version     => Mod.Version;
    public string Subtitle    => $"v{Mod.Version}   {string.Join(", ", Mod.Authors)}".TrimEnd();
    public bool   IsHealthy   => Mod.IsHealthy;
    public bool   HasIssue    => Mod.Issues.Count > 0;
    public string IssueText   => string.Join("\n", Mod.Issues.ConvertAll(i => $"[{i.Severity}] {i.Message}"));

    private bool _isEnabled = true;
    public bool IsEnabled
    {
        get => _isEnabled;
        set { _isEnabled = value; OnChanged(nameof(IsEnabled)); }
    }

    private bool _isChecked;
    public bool IsChecked
    {
        get => _isChecked;
        set { _isChecked = value; OnChanged(nameof(IsChecked)); }
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnChanged([CallerMemberName] string? n = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(n));
}

public sealed class MainViewModel : INotifyPropertyChanged
{
    private readonly LauncherConfig          _config;
    private readonly string                  _baseDir;
    private readonly IReadOnlyList<ModInfo>  _allMods;

    public string GameTitle      => _config.GameTitle;
    public string RuntimeExeName => _config.RuntimeExecutable;
    public string BaseDirDisplay => _baseDir;

    public ObservableCollection<ModItemVM> Games  { get; } = new();
    public ObservableCollection<ModItemVM> Addons { get; } = new();

    private ModItemVM? _selectedGame;
    public ModItemVM? SelectedGame
    {
        get => _selectedGame;
        set
        {
            _selectedGame = value;
            OnChanged(nameof(SelectedGame));
            OnChanged(nameof(SelectedDescription));
            OnChanged(nameof(CanLaunch));
            RefreshAddonAvailability();
        }
    }

    public string SelectedDescription
    {
        get
        {
            if (SelectedGame is null) return "（未选择）";
            var m = SelectedGame.Mod;
            var lines = new List<string>
            {
                m.DisplayName + "  v" + m.Version,
                "ID: " + m.Id,
                "作者: " + (m.Authors.Count > 0 ? string.Join(", ", m.Authors) : "(无)"),
                "引擎要求: ≥ " + m.EngineMin + "    ScriptApi: ≥ " + m.ScriptApiMin,
                "管线: " + string.Join(", ", m.SupportedPipelines),
            };
            if (!string.IsNullOrEmpty(m.Description)) lines.Add("\n" + m.Description);
            if (m.Issues.Count > 0)
            {
                lines.Add("");
                foreach (var i in m.Issues) lines.Add($"[{i.Severity}] {i.Message}");
            }
            return string.Join("\n", lines);
        }
    }

    public ObservableCollection<string> Pipelines { get; } = new();

    private string _selectedPipeline = "forward";
    public string SelectedPipeline
    {
        get => _selectedPipeline;
        set
        {
            _selectedPipeline = value;
            OnChanged(nameof(SelectedPipeline));
            OnChanged(nameof(CanLaunch));
            RefreshAddonAvailability();
        }
    }

    public bool CanLaunch =>
        SelectedGame is not null
        && SelectedGame.Mod.IsHealthy
        && SelectedGame.Mod.SupportedPipelines.Contains(SelectedPipeline);

    public string ValidationSummary
    {
        get
        {
            if (SelectedGame is null) return "";
            var picked = Addons.Where(a => a.IsChecked).Select(a => a.Mod).ToList();
            var msgs = Validator.ValidateCombo(
                SelectedGame.Mod, picked, SelectedPipeline,
                EngineVersionStub, ScriptApiVersionStub, _allMods).ToList();
            if (msgs.Count == 0) return "✓ 通过校验";
            return "⚠ " + string.Join("\n⚠ ", msgs);
        }
    }

    // TODO(v0.4): replace with values queried from runtime exe (`StarArkSamples.exe --version`)
    public const string EngineVersionStub    = "0.3.0";
    public const int    ScriptApiVersionStub = 2;

    public MainViewModel(LauncherConfig config, string baseDir, IReadOnlyList<ModInfo> mods)
    {
        _config  = config;
        _baseDir = baseDir;
        _allMods = mods;

        Pipelines.Add("forward");
        if (config.AllowDeferred) Pipelines.Add("deferred");
        SelectedPipeline = config.DefaultPipeline;

        foreach (var m in mods.Where(m => m.IsGame).OrderBy(m => m.DisplayName))
            Games.Add(new ModItemVM(m));
        foreach (var m in mods.Where(m => m.IsAddon).OrderBy(m => m.DisplayName))
        {
            var vm = new ModItemVM(m);
            vm.PropertyChanged += (_, e) =>
            {
                if (e.PropertyName == nameof(ModItemVM.IsChecked))
                    OnChanged(nameof(ValidationSummary));
            };
            Addons.Add(vm);
        }

        if (Games.Count > 0) SelectedGame = Games[0];
    }

    private void RefreshAddonAvailability()
    {
        foreach (var a in Addons)
        {
            var ok = SelectedGame is not null
                  && (a.Mod.AppliesTo.Count == 0 || a.Mod.AppliesTo.Contains(SelectedGame.Mod.Id))
                  && a.Mod.IsHealthy
                  && a.Mod.SupportedPipelines.Contains(SelectedPipeline);
            a.IsEnabled = ok;
            if (!ok) a.IsChecked = false;
        }
        OnChanged(nameof(ValidationSummary));
    }

    public IReadOnlyList<ModInfo> SelectedAddons() =>
        Addons.Where(a => a.IsChecked && a.IsEnabled).Select(a => a.Mod).ToList();

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnChanged(string n) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(n));
}
