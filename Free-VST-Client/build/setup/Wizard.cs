using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace Zuena.Setup
{
    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Native.EnableDpiAwareness();
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Wizard(Application.ExecutablePath, Install.Destination,
                Install.MenuShortcut, Install.DesktopShortcut));
        }
    }

    internal sealed class Wizard : Form
    {
        private enum Page { Welcome, Options, Installing, Done }

        private static readonly Color Surface = Color.FromArgb(24, 26, 28);
        private static readonly Color Ink = Color.FromArgb(245, 245, 243);
        private static readonly Color Muted = Color.FromArgb(178, 183, 187);
        private static readonly Color Mint = Color.FromArgb(171, 247, 219);

        private readonly Artwork art;
        private readonly Label step;
        private readonly Label title;
        private readonly Label detail;
        private readonly Label path;
        private readonly Label status;
        private readonly CheckBox desktop;
        private readonly CheckBox launch;
        private readonly ProgressBar progress;
        private readonly Button next;
        private readonly Button back;
        private readonly Button exit;
        private readonly string installer;
        private readonly string destination;
        private readonly string menuShortcut;
        private readonly string desktopShortcut;
        private Page page;
        private CancellationTokenSource cancellation;
        private bool committing;

        internal Wizard(string installer, string destination, string menuShortcut, string desktopShortcut)
        {
            this.installer = installer;
            this.destination = destination;
            this.menuShortcut = menuShortcut;
            this.desktopShortcut = desktopShortcut;
            Text = "Instalador de ZUENA Plugins";
            Font = new Font("Segoe UI", 10);
            BackColor = Surface;
            ForeColor = Ink;
            AutoScaleDimensions = new SizeF(96, 96);
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(800, 488);
            StartPosition = FormStartPosition.CenterScreen;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);

            art = new Artwork { Bounds = new Rectangle(0, 0, 264, 488), TabStop = false };
            Controls.Add(art);
            string version = Assembly.GetExecutingAssembly().GetName().Version.ToString(3);
            LabelAt("ZUENA PLUGINS   /   " + version + "   /   WINDOWS x64", 300, 29, 463, 25, 9, Muted);
            step = LabelAt("", 300, 79, 463, 25, 10, Mint);
            title = LabelAt("", 298, 115, 468, 49, 26, Ink, FontStyle.Bold);
            detail = LabelAt("", 300, 182, 460, 74, 11, Muted);
            path = LabelAt("", 300, 263, 458, 53, 10, Ink);
            status = LabelAt("", 300, 347, 460, 65, 10, Mint);
            status.AccessibleRole = AccessibleRole.Alert;

            desktop = Choice("Crear un acceso directo en el escritorio", 315);
            launch = Choice("Abrir ZUENA Plugins al terminar", 267);
            progress = new ProgressBar
            {
                Bounds = new Rectangle(300, 310, 458, 11), AccessibleName = "Progreso de instalación"
            };
            Controls.Add(progress);
            back = Action("Atrás", 300, 90, false);
            exit = Action("Cancelar", 507, 105, false);
            next = Action("Continuar", 623, 135, true);
            back.Click += delegate { ShowPage(Page.Welcome); };
            exit.Click += delegate { if (page == Page.Installing) CancelWork(); else Close(); };
            next.Click += Advance;
            AcceptButton = next;
            CancelButton = exit;
            FormClosing += OnClosing;
            Shown += delegate { next.Focus(); };
            ShowPage(Page.Welcome);
        }

        private Label LabelAt(string text, int x, int y, int width, int height, int size, Color color,
            FontStyle style = FontStyle.Regular)
        {
            var label = new Label
            {
                Text = text, Bounds = new Rectangle(x, y, width, height), ForeColor = color,
                Font = new Font("Segoe UI", size, style), UseMnemonic = false
            };
            Controls.Add(label);
            return label;
        }

        private CheckBox Choice(string text, int y)
        {
            var box = new CheckBox
            {
                Text = text, Checked = true, ForeColor = Ink,
                Bounds = new Rectangle(300, y, 458, 32), UseVisualStyleBackColor = false
            };
            Controls.Add(box);
            return box;
        }

        private Button Action(string text, int x, int width, bool primary)
        {
            var button = new Button
            {
                Text = text, Bounds = new Rectangle(x, 426, width, 38), FlatStyle = FlatStyle.Flat,
                BackColor = primary ? Ink : Surface, ForeColor = primary ? Surface : Ink,
                Cursor = Cursors.Hand, UseVisualStyleBackColor = false
            };
            button.FlatAppearance.BorderColor = primary ? Ink : Color.FromArgb(87, 92, 95);
            Controls.Add(button);
            return button;
        }

        private void ShowPage(Page value)
        {
            page = value;
            art.SetStage((int)page);
            step.Text = new[] { "01   BIENVENIDA", "02   OPCIONES", "03   INSTALACIÓN", "04   LISTO" }[(int)page];
            title.Text = new[] { "Instalá ZUENA Plugins", "Elegí tus accesos", "Instalando ZUENA", "ZUENA ya está listo" }[(int)page];
            detail.Text = new[]
            {
                "Tu acceso a los plugins de ZUENA para producción musical.\r\nEste asistente instala el launcher en tu cuenta de Windows.",
                "Encontralo desde Inicio. También podés agregar un\r\nacceso directo en el escritorio.",
                "El asistente verifica el paquete y prepara los archivos.",
                "Encontrá ZUENA Plugins en el menú Inicio. Abrilo para\r\nconfigurar el launcher y explorar el catálogo."
            }[(int)page];
            path.Visible = page == Page.Options;
            path.Text = path.Visible ? "Carpeta de instalación\r\n" + destination : "";
            desktop.Visible = page == Page.Options;
            launch.Visible = page == Page.Done;
            progress.Visible = page == Page.Installing;
            back.Visible = page == Page.Options;
            next.Visible = page != Page.Installing;
            exit.Visible = page != Page.Done;
            exit.Enabled = true;
            exit.Text = "Cancelar";
            next.Text = new[] { "Continuar", "Instalar", "", "Finalizar" }[(int)page];
            status.Text = "";
        }

        private async void Advance(object sender, EventArgs args)
        {
            if (page == Page.Welcome)
            {
                ShowPage(Page.Options);
                next.Focus();
                return;
            }
            if (page == Page.Done)
            {
                try
                {
                    if (launch.Checked)
                    {
                        var app = Path.Combine(destination, Install.Executable);
                        using (Process.Start(new ProcessStartInfo(app) { WorkingDirectory = destination, UseShellExecute = true })) { }
                    }
                    Close();
                }
                catch (Win32Exception)
                {
                    status.Text = "Windows no pudo abrir el launcher. Podés reintentar o finalizar sin abrirlo.";
                }
                return;
            }
            if (page != Page.Options) return;

            bool addDesktop = desktop.Checked;
            committing = false;
            progress.Value = 0;
            ShowPage(Page.Installing);
            exit.Focus();

            using (var source = new CancellationTokenSource())
            {
                cancellation = source;
                int last = -1;
                try
                {
                    await Task.Run(() => Install.Run(installer, destination, menuShortcut,
                        addDesktop ? desktopShortcut : null, (percent, message) =>
                        {
                            if (percent == last) return;
                            last = percent;
                            BeginInvoke((Action)(() =>
                            {
                                if (page != Page.Installing || source.IsCancellationRequested) return;
                                progress.Value = percent;
                                status.Text = message;
                            }));
                        }, () => Invoke((Action)(() =>
                        {
                            source.Token.ThrowIfCancellationRequested();
                            committing = true;
                            exit.Enabled = false;
                        })), source.Token));
                    ShowPage(Page.Done);
                    next.Focus();
                }
                catch (OperationCanceledException)
                {
                    ShowPage(Page.Options);
                    status.Text = "Instalación cancelada.";
                    next.Focus();
                }
                catch (Exception error)
                {
                    ShowPage(Page.Options);
                    status.Text = error is TargetInvocationException && error.InnerException != null
                        ? error.InnerException.Message : error.Message;
                    next.Focus();
                }
                finally { cancellation = null; }
            }
        }

        private void CancelWork()
        {
            if (committing || cancellation == null) return;
            cancellation.Cancel();
            exit.Enabled = false;
            status.Text = "Cancelando…";
        }

        private void OnClosing(object sender, FormClosingEventArgs args)
        {
            if (page != Page.Installing) return;
            args.Cancel = true;
            CancelWork();
        }
    }

    internal sealed class Artwork : Control
    {
        private readonly Stopwatch clock = Stopwatch.StartNew();
        private readonly System.Windows.Forms.Timer motion;
        private int stage;

        internal Artwork()
        {
            DoubleBuffered = true;
            if (Native.MotionEnabled)
            {
                motion = new System.Windows.Forms.Timer { Interval = 33 };
                motion.Tick += delegate
                {
                    if (FindForm().WindowState != FormWindowState.Minimized) Invalidate();
                };
                motion.Start();
            }
        }

        internal void SetStage(int value)
        {
            stage = value;
            Invalidate();
        }

        protected override void OnPaint(PaintEventArgs args)
        {
            var g = args.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.ScaleTransform(Width / 264f, Height / 488f);
            using (var background = new LinearGradientBrush(new Rectangle(0, 0, 264, 488),
                Color.FromArgb(20, 41, 44), Color.FromArgb(16, 19, 21), 57f))
                g.FillRectangle(background, 0, 0, 265, 489);

            double phase = motion == null ? 0 : clock.Elapsed.TotalSeconds;
            using (var outline = new Pen(Color.FromArgb(38, 169, 247, 218)))
                for (int radius = 34; radius <= 146; radius += 14)
                    g.DrawEllipse(outline, 132 - radius, 247 - radius, 2 * radius, 2 * radius);

            using (var signal = new Pen(Color.FromArgb(177, 171, 247, 219), 1.25f))
            {
                var points = new PointF[109];
                for (int i = 0; i < points.Length; i++)
                {
                    float x = 24 + 2 * i;
                    double envelope = Math.Pow(Math.Sin(Math.PI * i / (points.Length - 1)), 1.5);
                    double wave = Math.Sin(x / 10 - phase * 2) + 0.36 * Math.Sin(x / 4 + phase * 2.5);
                    points[i] = new PointF(x, (float)(247 + (18 + 4 * stage) * envelope * wave));
                }
                g.DrawLines(signal, points);
            }

            using (var logo = new SolidBrush(Color.FromArgb(245, 245, 243)))
            using (var glyph = new Pen(Color.FromArgb(24, 26, 28), 4f) { LineJoin = LineJoin.Round })
            {
                g.FillEllipse(logo, 24, 28, 64, 64);
                g.DrawLines(glyph, new[] { new Point(43, 48), new Point(69, 48), new Point(43, 72), new Point(69, 72) });
            }
            using (var font = new Font("Segoe UI", 11, FontStyle.Bold, GraphicsUnit.Pixel))
            using (var highlighted = new SolidBrush(Color.FromArgb(171, 247, 219)))
            using (var pending = new SolidBrush(Color.FromArgb(138, 157, 157)))
            {
                string[] labels = { "Bienvenida", "Opciones", "Instalación", "Listo" };
                for (int i = 0; i < labels.Length; i++)
                {
                    var brush = i <= stage ? highlighted : pending;
                    g.FillEllipse(brush, 24, 386 + i * 19, 4, 4);
                    g.DrawString(labels[i], font, brush, 34, 380 + i * 19);
                }
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing && motion != null) motion.Dispose();
            base.Dispose(disposing);
        }
    }

    internal static class Native
    {
        [DllImport("user32.dll")]
        private static extern bool SystemParametersInfo(uint action, uint parameter, out int result, uint flags);

        [DllImport("user32.dll")]
        private static extern bool SetProcessDPIAware();

        internal static void EnableDpiAwareness() { SetProcessDPIAware(); }

        internal static bool MotionEnabled
        {
            get
            {
                int enabled;
                return SystemParametersInfo(0x1042, 0, out enabled, 0) && enabled != 0;
            }
        }
    }
}
