using System;
using System.IO;
using System.IO.Compression;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Windows.Forms;

namespace Zuena.Setup
{
    internal static class InstallChecks
    {
        [STAThread]
        private static void Main(string[] args)
        {
            if (args[0] == "fixture")
            {
                using (var zip = new ZipArchive(File.Create(args[1]), ZipArchiveMode.Create))
                using (var data = new StreamWriter(zip.CreateEntry("ZUENA Plugins/../escape.txt").Open()))
                    data.Write("Entrada inválida");
                return;
            }

            string installer = args[1];
            string root = args[2];
            string destination = Path.Combine(root, "Programs", Install.Product);
            string menu = Path.Combine(root, "Inicio", "ZUENA Plugins.lnk");
            string desktop = Path.Combine(root, "Escritorio", "ZUENA Plugins.lnk");
            string exe = Path.Combine(destination, Install.Executable);
            var silent = new Action<int, string>((p, s) => { });

            Install.Run(installer, destination, menu, null, silent, () => { }, CancellationToken.None);
            Check(File.Exists(exe) && new FileInfo(exe).Length > 100 * 1024 * 1024, "Runtime Electron completo");
            Check(File.Exists(Path.Combine(destination, "resources", "app.asar")), "Aplicación completa");
            Check(File.Exists(menu) && !File.Exists(desktop), "Acceso de Inicio con escritorio desactivado");

            string marker = Path.Combine(destination, "original.txt");
            File.WriteAllText(marker, "Copia anterior");
            string incomplete = Path.Combine(root, "incompleto.exe");
            File.WriteAllText(incomplete, "Archivo incompleto");
            Expect<InvalidDataException>(() => Install.Run(incomplete, destination, menu, desktop, silent, () => { }, CancellationToken.None));
            Check(File.Exists(marker), "Paquete incompleto conserva la instalación anterior");
            Expect<InvalidDataException>(() => Install.Run(args[4], destination, menu, desktop, silent, () => { }, CancellationToken.None));
            Check(File.Exists(marker), "SHA-256 alterado conserva la instalación anterior");

            Expect<InvalidDataException>(() => Install.Run(args[3], destination, menu, desktop, silent, () => { }, CancellationToken.None));
            Check(!File.Exists(Path.Combine(root, "escape.txt")), "Rechaza traversal");

            using (var cancel = new CancellationTokenSource())
                Expect<OperationCanceledException>(() => Install.Run(installer, destination, menu, desktop,
                    (percent, message) => { if (percent >= 32) cancel.Cancel(); }, () => { }, cancel.Token));
            Check(File.Exists(marker) && !File.Exists(desktop), "Cancelar durante la extracción conserva archivos y accesos");

            Directory.CreateDirectory(desktop);
            var menuBefore = File.ReadAllBytes(menu);
            Expect<IOException>(() => Install.Run(installer, destination, menu, desktop, silent, () => { }, CancellationToken.None));
            Check(File.ReadAllText(marker) == "Copia anterior", "Restaura la instalación anterior cuando falla un acceso");
            Check(Convert.ToBase64String(menuBefore) == Convert.ToBase64String(File.ReadAllBytes(menu)), "Restaura el acceso directo anterior");
            Directory.Delete(desktop);

            Install.Run(installer, destination, menu, desktop, silent, () => { }, CancellationToken.None);
            Check(!File.Exists(marker) && File.Exists(desktop), "Reinstala con acceso de escritorio");
            Check(Directory.GetDirectories(Path.GetDirectoryName(destination), ".zuena-setup-*").Length == 0,
                "Elimina las áreas de extracción");

            File.Delete(desktop);
            Native.EnableDpiAwareness();
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            using (var wizard = new Wizard(installer, destination, menu, desktop))
            {
                wizard.Show();
                Click(wizard, "Continuar");
                Click(wizard, "Atrás");
                Click(wizard, "Continuar");
                wizard.Controls.OfType<CheckBox>().Single(box => box.Visible).Checked = false;
                Click(wizard, "Instalar");
                var clock = Stopwatch.StartNew();
                while (FindButton(wizard, "Finalizar") == null && clock.Elapsed < TimeSpan.FromSeconds(45))
                {
                    if (FindButton(wizard, "Instalar") != null)
                        throw new Exception("El asistente volvió a opciones antes de terminar.");
                    Application.DoEvents();
                    Thread.Sleep(15);
                }
                Check(FindButton(wizard, "Finalizar") != null, "El asistente pasa por bienvenida, opciones, instalación y cierre");
                Check(File.Exists(exe) && File.Exists(menu) && !File.Exists(desktop), "La opción de escritorio del asistente se aplica");
                wizard.Controls.OfType<CheckBox>().Single(box => box.Visible).Checked = false;
                Click(wizard, "Finalizar");
                Check(!wizard.Visible, "Finaliza sin abrir el launcher cuando se desmarca la opción");
            }
        }

        private static Button FindButton(Wizard wizard, string text)
        {
            return wizard.Controls.OfType<Button>().SingleOrDefault(button => button.Visible && button.Text == text);
        }

        private static void Click(Wizard wizard, string text)
        {
            var button = FindButton(wizard, text);
            Check(button != null && button.Enabled, "Botón habilitado: " + text);
            button.PerformClick();
        }

        private static void Check(bool condition, string message)
        {
            if (!condition) throw new Exception(message);
            Console.WriteLine("OK " + message);
        }

        private static void Expect<T>(Action action) where T : Exception
        {
            try { action(); }
            catch (T) { return; }
            throw new Exception("Se esperaba " + typeof(T).Name);
        }
    }
}
