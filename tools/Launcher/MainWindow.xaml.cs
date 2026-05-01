using System;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using StarArk.Launcher.Models;
using StarArk.Launcher.Services;
using StarArk.Launcher.ViewModels;

namespace StarArk.Launcher;

public partial class MainWindow : Window
{
    private readonly string         _baseDir;
    private readonly LauncherConfig _config;
    private readonly ModScanner     _scanner = new();
    private MainViewModel           _vm;

    public MainWindow()
    {
        InitializeComponent();
        _baseDir = AppContext.BaseDirectory;
        _config  = LauncherConfig.LoadOrDefault(_baseDir);
        _vm      = BuildViewModel();
        DataContext = _vm;
        BuildPipelineRadios();
    }

    private MainViewModel BuildViewModel()
    {
        var modsRoot = Path.Combine(_baseDir, _config.ModsDirectory);
        var mods = _scanner.Scan(modsRoot);
        return new MainViewModel(_config, _baseDir, mods);
    }

    private void BuildPipelineRadios()
    {
        // Remove previously generated radios (keep the leading TextBlock).
        for (int i = PipelinePanel.Children.Count - 1; i >= 1; i--)
            PipelinePanel.Children.RemoveAt(i);

        foreach (var p in _vm.Pipelines)
        {
            var rb = new RadioButton
            {
                GroupName = "Pipeline",
                Content   = p,
                Tag       = p,
                IsChecked = p == _vm.SelectedPipeline,
            };
            rb.Checked += (_, _) => { if (rb.Tag is string s) _vm.SelectedPipeline = s; };
            PipelinePanel.Children.Add(rb);
        }
    }

    // ===== Buttons =====
    private void Refresh_Click(object sender, RoutedEventArgs e)
    {
        _vm = BuildViewModel();
        DataContext = _vm;
        BuildPipelineRadios();
    }

    private void OpenModsFolder_Click(object sender, RoutedEventArgs e)
    {
        var path = Path.Combine(_baseDir, _config.ModsDirectory);
        if (!Directory.Exists(path)) Directory.CreateDirectory(path);
        Process.Start(new ProcessStartInfo("explorer.exe", path) { UseShellExecute = true });
    }

    private void Quit_Click(object sender, RoutedEventArgs e) => Close();

    private void Launch_Click(object sender, RoutedEventArgs e)
    {
        if (_vm.SelectedGame is null) return;
        var svc = new LaunchService(_config, _baseDir);
        if (svc.TryLaunch(_vm.SelectedGame.Mod, _vm.SelectedAddons(), _vm.SelectedPipeline, out var err))
            Application.Current.Shutdown();
        else
            MessageBox.Show(this, err, "启动失败", MessageBoxButton.OK, MessageBoxImage.Error);
    }
}
