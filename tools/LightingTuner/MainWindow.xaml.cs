using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;

namespace StarArk.LightingTuner;

public partial class MainWindow : Window
{
    private string? _path;
    private JsonObject? _root;
    private DateTime _lastMTime;
    private bool _suspendEvents;
    private bool _selfWrite;
    private DateTime _lastAutoSave = DateTime.MinValue;
    private readonly DispatcherTimer _watcher;

    public MainWindow()
    {
        InitializeComponent();
        _watcher = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _watcher.Tick += (_, _) => PollExternalChange();
        _watcher.Start();

        Loaded += (_, _) =>
        {
            var initial = FindDefaultJson();
            if (initial != null) OpenFile(initial);
            else Status("未找到默认 lighting.json，请点【浏览…】选择");
        };
    }

    // ---- file discovery ------------------------------------------------

    private static string? FindDefaultJson()
    {
        var start = AppContext.BaseDirectory;
        var probes = new[]
        {
            @"build\samples\content\lighting.json",
            @"build\game\content\lighting.json",
            @"content\lighting.json",
        };
        var dir = new DirectoryInfo(start);
        for (int i = 0; i < 8 && dir != null; i++, dir = dir.Parent)
        {
            foreach (var rel in probes)
            {
                var full = Path.Combine(dir.FullName, rel);
                if (File.Exists(full)) return full;
            }
        }
        return null;
    }

    // ---- open / save / reload -----------------------------------------

    private void OpenFile(string path)
    {
        try
        {
            var text = File.ReadAllText(path);
            var node = JsonNode.Parse(text) as JsonObject
                       ?? throw new InvalidDataException("root is not an object");
            _root = node;
            _path = path;
            _lastMTime = File.GetLastWriteTimeUtc(path);
            PathText.Text = path;
            RebuildTabs();
            Status($"已加载 {Path.GetFileName(path)}");
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, $"无法打开:\n{path}\n\n{ex.Message}",
                "Lighting Tuner", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void Save()
    {
        if (_path == null || _root == null) return;
        try
        {
            var opts = new JsonSerializerOptions { WriteIndented = true };
            var text = _root.ToJsonString(opts);
            var tmp = _path + ".tmp";
            File.WriteAllText(tmp, text);
            File.Move(tmp, _path, overwrite: true);
            _selfWrite = true;
            _lastMTime = File.GetLastWriteTimeUtc(_path);
            _lastAutoSave = DateTime.UtcNow;
            Status($"已保存 {Path.GetFileName(_path)}  ({DateTime.Now:HH:mm:ss})");
        }
        catch (Exception ex)
        {
            Status("保存失败: " + ex.Message);
        }
    }

    private void PollExternalChange()
    {
        if (_path == null || !File.Exists(_path)) return;
        var mt = File.GetLastWriteTimeUtc(_path);
        if (mt == _lastMTime) return;
        if (_selfWrite) { _selfWrite = false; _lastMTime = mt; return; }
        OpenFile(_path);
    }

    private void OnApply(object sender, RoutedEventArgs e) => Save();

    private void OnReload(object sender, RoutedEventArgs e)
    {
        if (_path != null) OpenFile(_path);
    }

    private void OnBrowse(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog
        {
            Filter = "lighting.json|lighting.json|JSON|*.json|All files|*.*",
            Title = "选择 lighting.json",
        };
        if (dlg.ShowDialog(this) == true) OpenFile(dlg.FileName);
    }

    // ---- UI construction ---------------------------------------------

    private void RebuildTabs()
    {
        _suspendEvents = true;
        Tabs.Items.Clear();
        if (_root == null) { _suspendEvents = false; return; }

        Tabs.Items.Add(new TabItem { Header = "渲染 Render", Content = BuildRenderTab() });

        if (_root["lights"] is JsonArray lights)
        {
            foreach (var l in lights)
            {
                if (l is not JsonObject lo) continue;
                var name = (string?)lo["name"] ?? (string?)lo["type"] ?? "?";
                Tabs.Items.Add(new TabItem { Header = name, Content = BuildLightTab(lo) });
            }
        }
        _suspendEvents = false;
    }

    // ===== defaults (must mirror engine/rendering/RenderSettings.h & Light.h) =====

    private static class Defaults
    {
        // RenderSettings
        public const double Exposure = 1.0;
        public const bool   BloomEnabled    = true;
        public const double BloomThreshold  = 1.0;
        public const double BloomStrength   = 0.6;
        public const int    BloomIterations = 5;
        public const bool   SkyEnabled      = true;
        public const double SkyIntensity    = 1.0;
        public const bool   IblEnabled          = true;
        public const double IblDiffuseIntensity = 1.0;
        public const double IblSpecularIntensity = 1.0;
        public const bool   ShadowEnabled       = true;
        public const double ShadowOrthoHalfSize = 25.0;
        public const double ShadowDepthBias     = 0.002;
        public const double ShadowNormalBias    = 0.010;
        public const int    ShadowPcfKernel     = 2;

        // Light (per Light.h)
        public const double LightIntensity = 1.0;
        public const double LightRange     = 10.0;
        public const double LightConstant  = 1.0;
        public const double LightLinear    = 0.09;
        public const double LightQuadratic = 0.032;
        public const double LightSpotInner = 12.5;
        public const double LightSpotOuter = 17.5;
        public static readonly double[] LightColor   = { 1, 1, 1 };
        public static readonly double[] LightAmbient = { 0.1, 0.1, 0.1 };
        public static readonly double[] Vec3Zero     = { 0, 0, 0 };
    }

    private FrameworkElement BuildRenderTab()
    {
        var sv = new ScrollViewer { VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        var stack = new StackPanel();
        sv.Content = stack;

        var rs = GetOrCreateObject(_root!, "renderSettings");

        stack.Children.Add(SliderRow("Exposure 曝光",   rs, "exposure", 0, 4, 0.01, Defaults.Exposure));

        var bloom = GetOrCreateObject(rs, "bloom");
        stack.Children.Add(Group("Bloom 泛光",
            BoolRow("enabled 启用", bloom, "enabled", Defaults.BloomEnabled),
            SliderRow("threshold 阈值",    bloom, "threshold",  0, 4, 0.01, Defaults.BloomThreshold),
            SliderRow("strength 强度",     bloom, "strength",   0, 2, 0.01, Defaults.BloomStrength),
            SliderRow("iterations 迭代次数", bloom, "iterations", 1, 8, 1,   Defaults.BloomIterations)));

        var sky = GetOrCreateObject(rs, "sky");
        stack.Children.Add(Group("Sky 天空盒",
            BoolRow("enabled 启用", sky, "enabled", Defaults.SkyEnabled),
            SliderRow("intensity 强度", sky, "intensity", 0, 4, 0.01, Defaults.SkyIntensity)));

        var ibl = GetOrCreateObject(rs, "ibl");
        stack.Children.Add(Group("IBL 基于图像的光照",
            BoolRow("enabled 启用", ibl, "enabled", Defaults.IblEnabled),
            SliderRow("diffuseIntensity 漫反射强度",  ibl, "diffuseIntensity",  0, 4, 0.01, Defaults.IblDiffuseIntensity),
            SliderRow("specularIntensity 镜面反射强度", ibl, "specularIntensity", 0, 4, 0.01, Defaults.IblSpecularIntensity)));

        var shadow = GetOrCreateObject(rs, "shadow");
        stack.Children.Add(Group("Shadow 阴影",
            BoolRow("enabled 启用", shadow, "enabled", Defaults.ShadowEnabled),
            SliderRow("orthoHalfSize 正交半尺寸", shadow, "orthoHalfSize", 1, 200, 0.5, Defaults.ShadowOrthoHalfSize),
            SliderRow("depthBias 深度偏移",       shadow, "depthBias",  0, 0.02, 0.0001, Defaults.ShadowDepthBias),
            SliderRow("normalBias 法线偏移",      shadow, "normalBias", 0, 0.05, 0.0001, Defaults.ShadowNormalBias),
            SliderRow("pcfKernel PCF 核",         shadow, "pcfKernel",  0, 5, 1, Defaults.ShadowPcfKernel)));

        return sv;
    }

    private FrameworkElement BuildLightTab(JsonObject l)
    {
        var sv = new ScrollViewer { VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        var stack = new StackPanel();
        sv.Content = stack;

        var type = (string?)l["type"] ?? "Directional";
        var name = (string?)l["name"] ?? "?";
        var typeCn = type switch
        {
            "Directional" => "方向光",
            "Point" => "点光源",
            "Spot" => "聚光灯",
            _ => type,
        };
        stack.Children.Add(new TextBlock
        {
            Text = $"{name}  [{type} / {typeCn}]",
            FontWeight = FontWeights.Bold, Margin = new Thickness(6, 4, 0, 6),
        });

        stack.Children.Add(ColorRow("color 颜色",   GetOrCreateVec3(l, "color", 1, 1, 1),   Defaults.LightColor));

        stack.Children.Add(Group("General 通用",
            SliderRow("intensity 强度", l, "intensity", 0, 100, 0.05, Defaults.LightIntensity),
            ColorRow("ambient 环境色", GetOrCreateVec3(l, "ambient", 0.1, 0.1, 0.1), Defaults.LightAmbient)));

        if (type is "Point" or "Spot")
        {
            var pos = GetOrCreateVec3(l, "position", 0, 0, 0);
            stack.Children.Add(Group("Position 位置",
                Vec3Row(pos, -50, 50, 0.01, Defaults.Vec3Zero)));
        }

        if (type is "Directional" or "Spot")
        {
            var rot = GetOrCreateVec3(l, "rotationEuler", 0, 0, 0);
            stack.Children.Add(Group("Rotation (Euler°) 旋转（欧拉角°）",
                Vec3Row(rot, -180, 180, 0.5, Defaults.Vec3Zero)));
        }

        if (type is "Point" or "Spot")
        {
            stack.Children.Add(Group("Attenuation 衰减",
                SliderRow("range 作用范围", l, "range",     0.1, 100, 0.1,   Defaults.LightRange),
                SliderRow("constant 常数项", l, "constant", 0.0, 2.0, 0.01,  Defaults.LightConstant),
                SliderRow("linear 一次项",   l, "linear",   0.0, 2.0, 0.001, Defaults.LightLinear),
                SliderRow("quadratic 二次项", l, "quadratic", 0.0, 2.0, 0.001, Defaults.LightQuadratic)));
        }

        if (type == "Spot")
        {
            stack.Children.Add(Group("Spot Angles (°) 聚光角度",
                SliderRow("innerAngle 内锥角", l, "innerAngle", 0, 89, 0.1, Defaults.LightSpotInner),
                SliderRow("outerAngle 外锥角", l, "outerAngle", 0, 89, 0.1, Defaults.LightSpotOuter)));
        }

        return sv;
    }

    // ---- control factories -------------------------------------------

    private GroupBox Group(string header, params UIElement[] children)
    {
        var sp = new StackPanel();
        foreach (var c in children) sp.Children.Add(c);
        return new GroupBox { Header = header, Content = sp };
    }

    private FrameworkElement SliderRow(string label, JsonObject container, string key,
                                       double lo, double hi, double res, double defaultValue)
    {
        var grid = BuildRowGrid();

        grid.Children.Add(new TextBlock
        {
            Text = label, VerticalAlignment = VerticalAlignment.Center,
        });
        var slider = new Slider { Minimum = lo, Maximum = hi };
        if (res > 0) { slider.SmallChange = res; slider.LargeChange = res * 10; }
        var box = new TextBox
        {
            Width = 64, VerticalAlignment = VerticalAlignment.Center,
            TextAlignment = TextAlignment.Right,
        };
        var resetBtn = BuildResetButton($"重置为默认值 {defaultValue}");

        double Read() => (container[key] as JsonValue)?.GetValue<double>() ?? 0.0;
        void Write(double v)
        {
            if (res > 0) v = Math.Round(v / res) * res;
            container[key] = v;
            OnValueChanged();
        }
        void SetUiFromValue(double v)
        {
            _suspendEvents = true;
            slider.Value = Math.Max(lo, Math.Min(hi, v));
            box.Text = v.ToString("0.###");
            _suspendEvents = false;
        }

        SetUiFromValue(Read());

        slider.ValueChanged += (_, _) =>
        {
            if (_suspendEvents) return;
            Write(slider.Value);
            box.Text = slider.Value.ToString("0.###");
        };
        box.LostFocus += (_, _) =>
        {
            if (double.TryParse(box.Text, out var v))
            {
                Write(v);
                SetUiFromValue(v);
            }
            else box.Text = Read().ToString("0.###");
        };
        box.KeyDown += (_, e) =>
        {
            if (e.Key == Key.Enter) Keyboard.ClearFocus();
        };
        resetBtn.Click += (_, _) =>
        {
            Write(defaultValue);
            SetUiFromValue(defaultValue);
        };

        Grid.SetColumn(slider, 1);   grid.Children.Add(slider);
        Grid.SetColumn(box, 2);      grid.Children.Add(box);
        Grid.SetColumn(resetBtn, 3); grid.Children.Add(resetBtn);
        return grid;
    }

    private FrameworkElement BoolRow(string label, JsonObject container, string key, bool defaultValue)
    {
        var grid = BuildRowGrid();
        var cb = new CheckBox
        {
            Content = label, VerticalAlignment = VerticalAlignment.Center,
            IsChecked = (container[key] as JsonValue)?.GetValue<bool>() ?? defaultValue,
        };
        cb.Click += (_, _) =>
        {
            if (_suspendEvents) return;
            container[key] = cb.IsChecked == true;
            OnValueChanged();
        };
        var resetBtn = BuildResetButton($"重置为默认值 {defaultValue}");
        resetBtn.Click += (_, _) =>
        {
            _suspendEvents = true;
            cb.IsChecked = defaultValue;
            _suspendEvents = false;
            container[key] = defaultValue;
            OnValueChanged();
        };

        grid.Children.Add(cb);              // col 0 (spans naturally)
        Grid.SetColumn(resetBtn, 3); grid.Children.Add(resetBtn);
        return grid;
    }

    private FrameworkElement Vec3Row(JsonArray arr, double lo, double hi, double res, double[] defaults)
    {
        var sp = new StackPanel();
        string[] axes = ["x", "y", "z"];
        for (int i = 0; i < 3; i++)
        {
            int idx = i;
            sp.Children.Add(Vec3Slider(axes[i], arr, idx, lo, hi, res, defaults[i]));
        }
        return sp;
    }

    private FrameworkElement Vec3Slider(string label, JsonArray arr, int index,
                                        double lo, double hi, double res, double defaultValue)
    {
        var grid = BuildRowGrid();

        grid.Children.Add(new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center });
        var slider = new Slider { Minimum = lo, Maximum = hi };
        var box = new TextBox { Width = 64, TextAlignment = TextAlignment.Right };
        var resetBtn = BuildResetButton($"重置为默认值 {defaultValue}");

        double Read() => (arr[index] as JsonValue)?.GetValue<double>() ?? 0.0;
        void Write(double v)
        {
            if (res > 0) v = Math.Round(v / res) * res;
            arr[index] = v;
            OnValueChanged();
        }
        void SetUi(double v)
        {
            _suspendEvents = true;
            slider.Value = Math.Max(lo, Math.Min(hi, v));
            box.Text = v.ToString("0.###");
            _suspendEvents = false;
        }

        SetUi(Read());

        slider.ValueChanged += (_, _) =>
        {
            if (_suspendEvents) return;
            Write(slider.Value);
            box.Text = slider.Value.ToString("0.###");
        };
        box.LostFocus += (_, _) =>
        {
            if (double.TryParse(box.Text, out var v)) { Write(v); SetUi(v); }
            else box.Text = Read().ToString("0.###");
        };
        resetBtn.Click += (_, _) => { Write(defaultValue); SetUi(defaultValue); };

        Grid.SetColumn(slider, 1); grid.Children.Add(slider);
        Grid.SetColumn(box, 2);    grid.Children.Add(box);
        Grid.SetColumn(resetBtn, 3); grid.Children.Add(resetBtn);
        return grid;
    }

    private FrameworkElement ColorRow(string label, JsonArray arr, double[] defaults)
    {
        var gb = new GroupBox { Header = label };
        var outer = new Grid();
        outer.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(48) });
        outer.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

        var swatch = new Border
        {
            Width = 40, Height = 40, Margin = new Thickness(4),
            BorderBrush = Brushes.Gray, BorderThickness = new Thickness(1),
        };
        void UpdateSwatch()
        {
            byte Clamp(double d) => (byte)Math.Max(0, Math.Min(255, d * 255));
            var r = Clamp((arr[0] as JsonValue)?.GetValue<double>() ?? 0);
            var g = Clamp((arr[1] as JsonValue)?.GetValue<double>() ?? 0);
            var b = Clamp((arr[2] as JsonValue)?.GetValue<double>() ?? 0);
            swatch.Background = new SolidColorBrush(Color.FromRgb(r, g, b));
        }
        UpdateSwatch();
        Grid.SetColumn(swatch, 0); outer.Children.Add(swatch);

        var sliders = new StackPanel();
        Grid.SetColumn(sliders, 1); outer.Children.Add(sliders);

        string[] ch = ["R", "G", "B"];
        for (int i = 0; i < 3; i++)
        {
            int idx = i;
            var row = BuildRowGrid();
            row.Children.Add(new TextBlock { Text = ch[i], VerticalAlignment = VerticalAlignment.Center });

            var slider = new Slider { Minimum = 0, Maximum = 4, SmallChange = 0.01, LargeChange = 0.1 };
            var box = new TextBox { Width = 64, TextAlignment = TextAlignment.Right };
            var resetBtn = BuildResetButton($"重置为默认值 {defaults[idx]}");

            double Read() => (arr[idx] as JsonValue)?.GetValue<double>() ?? 0.0;
            void SetUi(double v)
            {
                _suspendEvents = true;
                slider.Value = Math.Max(0, Math.Min(4, v));
                box.Text = v.ToString("0.###");
                _suspendEvents = false;
            }
            SetUi(Read());

            slider.ValueChanged += (_, _) =>
            {
                if (_suspendEvents) return;
                var v = Math.Round(slider.Value / 0.001) * 0.001;
                arr[idx] = v;
                box.Text = v.ToString("0.###");
                UpdateSwatch();
                OnValueChanged();
            };
            box.LostFocus += (_, _) =>
            {
                if (double.TryParse(box.Text, out var v))
                {
                    arr[idx] = v;
                    SetUi(v);
                    UpdateSwatch();
                    OnValueChanged();
                }
                else box.Text = Read().ToString("0.###");
            };
            resetBtn.Click += (_, _) =>
            {
                arr[idx] = defaults[idx];
                SetUi(defaults[idx]);
                UpdateSwatch();
                OnValueChanged();
            };

            Grid.SetColumn(slider, 1); row.Children.Add(slider);
            Grid.SetColumn(box, 2);    row.Children.Add(box);
            Grid.SetColumn(resetBtn, 3); row.Children.Add(resetBtn);
            sliders.Children.Add(row);
        }

        gb.Content = outer;
        return gb;
    }

    // ---- layout / small widgets --------------------------------------

    private static Grid BuildRowGrid()
    {
        var grid = new Grid { Margin = new Thickness(4, 2, 4, 2) };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(170) });  // label
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) }); // slider
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(70) });   // textbox
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(40) });   // reset btn
        return grid;
    }

    private static Button BuildResetButton(string tooltip)
    {
        return new Button
        {
            Content = "重置",
            FontSize = 10,
            Padding = new Thickness(4, 1, 4, 1),
            Margin = new Thickness(4, 0, 0, 0),
            VerticalAlignment = VerticalAlignment.Center,
            ToolTip = tooltip,
        };
    }

    // ---- helpers ------------------------------------------------------

    private void OnValueChanged()
    {
        if (_suspendEvents) return;
        if (AutoApplyCheck.IsChecked != true) { Status("已修改 (未保存)"); return; }
        var now = DateTime.UtcNow;
        if ((now - _lastAutoSave).TotalMilliseconds < 100) return;
        Save();
    }

    private void Status(string s) => StatusText.Text = s;

    private static JsonObject GetOrCreateObject(JsonObject parent, string key)
    {
        if (parent[key] is JsonObject existing) return existing;
        var created = new JsonObject();
        parent[key] = created;
        return created;
    }

    private static JsonArray GetOrCreateVec3(JsonObject parent, string key,
                                             double x, double y, double z)
    {
        if (parent[key] is JsonArray arr && arr.Count == 3) return arr;
        var created = new JsonArray(x, y, z);
        parent[key] = created;
        return created;
    }
}
