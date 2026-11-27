using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

namespace Zuena.Setup
{
    internal static class Install
    {
        internal const string Product = "ZUENA Plugins";
        internal const string Executable = Product + ".exe";
        private const string Icon = Product + ".ico";
        private const string Prefix = Product + "/";
        private const string Magic = "ZUENAS01";
        private const int FooterSize = 48;

        internal static string Destination
        {
            get
            {
                return Path.Combine(ShellFolder(Environment.SpecialFolder.LocalApplicationData), "Programs", Product);
            }
        }

        internal static string MenuShortcut
        {
            get { return Path.Combine(ShellFolder(Environment.SpecialFolder.Programs), Product + ".lnk"); }
        }

        internal static string DesktopShortcut
        {
            get { return Path.Combine(ShellFolder(Environment.SpecialFolder.DesktopDirectory), Product + ".lnk"); }
        }

        private static string ShellFolder(Environment.SpecialFolder folder)
        {
            var path = Environment.GetFolderPath(folder, Environment.SpecialFolderOption.DoNotVerify);
            if (String.IsNullOrWhiteSpace(path))
                throw new IOException("Windows no encontró la carpeta " + folder + " de este usuario.");
            return path;
        }

        internal static void Run(string installer, string destination, string menu, string desktop,
            Action<int, string> progress, Action beginCommit, CancellationToken token)
        {
            string parent = Path.GetDirectoryName(destination);
            Directory.CreateDirectory(parent);
            string workspace = Path.Combine(parent, ".zuena-setup-" + Guid.NewGuid().ToString("N"));
            string staged = Path.Combine(workspace, "app");
            string old = Path.Combine(workspace, "anterior");
            bool keepBackup = false;

            using (var mutex = new Mutex(false, @"Local\ZUENA.Plugins.Setup"))
            {
                bool entered;
                try { entered = mutex.WaitOne(0); }
                catch (AbandonedMutexException) { entered = true; }
                if (!entered) throw new IOException("Ya hay otra instalación de ZUENA Plugins en curso.");

                try
                {
                    RequireClosed(destination);
                    Directory.CreateDirectory(staged);
                    using (var stream = OpenPayload(installer, progress, token))
                    using (var zip = new ZipArchive(stream, ZipArchiveMode.Read, true))
                        Extract(zip, staged, progress, token);
                    ValidateLauncher(staged);
                    RequireClosed(destination);

                    // Resuelve la carrera entre cancelar y empezar los renombrados en el hilo de la ventana.
                    beginCommit();
                    token.ThrowIfCancellationRequested();
                    progress(96, "Aplicando los cambios…");
                    var shortcuts = new List<Shortcut>();
                    bool installed = false;
                    try
                    {
                        if (Directory.Exists(destination))
                        {
                            if ((File.GetAttributes(destination) & FileAttributes.ReparsePoint) != 0)
                                throw new IOException("La carpeta de ZUENA apunta a otra ubicación.");
                            Directory.Move(destination, old);
                        }
                        Directory.Move(staged, destination);
                        installed = true;
                        shortcuts.Add(new Shortcut(menu, destination));
                        if (desktop != null) shortcuts.Add(new Shortcut(desktop, destination));
                        foreach (var shortcut in shortcuts) shortcut.Commit();
                        RegisterUninstall(destination);
                    }
                    catch (Exception original)
                    {
                        var errors = new List<Exception> { original };
                        foreach (var shortcut in shortcuts.AsEnumerable().Reverse())
                            Restore(shortcut.Rollback, errors);
                        if (installed) Restore(() => Directory.Move(destination, staged), errors);
                        if (Directory.Exists(old)) Restore(() => Directory.Move(old, destination), errors);
                        if (errors.Count > 1)
                        {
                            keepBackup = true;
                            throw new IOException("Windows impidió restaurar los archivos. La copia de respaldo quedó en "
                                + workspace + ".", new AggregateException(errors));
                        }
                        throw;
                    }
                    finally
                    {
                        foreach (var shortcut in shortcuts) shortcut.Clean(keepBackup);
                    }
                    progress(100, "Instalación completa.");
                }
                finally
                {
                    if (!keepBackup) DeleteDirectory(workspace);
                    mutex.ReleaseMutex();
                }
            }
        }

        // Entrada de «Aplicaciones instaladas» (HKCU, sin administrador). Desinstalar abre el launcher con
        // --uninstall. Si Windows no deja escribirla, el launcher la vuelve a escribir al abrirse.
        // FREEVST_UNINSTALL_KEY redirige la clave en las pruebas.
        internal static void RegisterUninstall(string destination)
        {
            try
            {
                string keyPath = Environment.GetEnvironmentVariable("FREEVST_UNINSTALL_KEY");
                if (String.IsNullOrWhiteSpace(keyPath))
                    keyPath = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\" + Product;
                using (var key = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(keyPath))
                {
                    string exe = Path.Combine(destination, Executable);
                    key.SetValue("DisplayName", Product);
                    key.SetValue("DisplayVersion", Assembly.GetExecutingAssembly().GetName().Version.ToString(3));
                    key.SetValue("Publisher", "ZUENA");
                    key.SetValue("DisplayIcon", Path.Combine(destination, Icon));
                    key.SetValue("InstallLocation", destination);
                    key.SetValue("UninstallString", "\"" + exe + "\" --uninstall");
                    key.SetValue("URLInfoAbout", "https://github.com/kiannnx/zuena-plugins");
                    key.SetValue("NoModify", 1, Microsoft.Win32.RegistryValueKind.DWord);
                    key.SetValue("NoRepair", 1, Microsoft.Win32.RegistryValueKind.DWord);
                }
            }
            catch (Exception) { /* No es crítico: el launcher la escribe al arrancar. */ }
        }

        private static void Restore(Action action, List<Exception> errors)
        {
            try { action(); }
            catch (Exception error) { errors.Add(error); }
        }

        private static void RequireClosed(string destination)
        {
            string path = Path.Combine(destination, Executable);
            foreach (var process in Process.GetProcessesByName(Product))
            using (process)
            {
                try
                {
                    if (!process.HasExited && String.Equals(process.MainModule.FileName, path, StringComparison.OrdinalIgnoreCase))
                        throw new IOException("Cerrá ZUENA Plugins y volvé a intentar.");
                }
                catch (System.ComponentModel.Win32Exception) { /* Windows protege procesos de otras sesiones. */ }
                catch (InvalidOperationException) { /* Es posible que haya terminado desde la consulta inicial. */ }
            }
        }

        private static Stream OpenPayload(string installer, Action<int, string> progress, CancellationToken token)
        {
            var file = File.OpenRead(installer);
            try
            {
                using (var reader = new BinaryReader(file, Encoding.ASCII, true))
                {
                    long footerOffset = PayloadEnd(reader) - FooterSize;
                    if (footerOffset <= 0) throw InvalidPackage();
                    file.Position = footerOffset;
                    if (Encoding.ASCII.GetString(reader.ReadBytes(8)) != Magic) throw InvalidPackage();
                    long length = reader.ReadInt64();
                    var expected = reader.ReadBytes(32);
                    if (length <= 0 || length >= footerOffset) throw InvalidPackage();
                    var payload = new PayloadStream(file, footerOffset - length, length);

                    using (var sha = SHA256.Create())
                    {
                        var buffer = new byte[256 * 1024];
                        int count;
                        while ((count = payload.Read(buffer, 0, buffer.Length)) != 0)
                        {
                            token.ThrowIfCancellationRequested();
                            sha.TransformBlock(buffer, 0, count, null, 0);
                            progress((int)(20 * payload.Position / length), "Verificando los archivos del instalador…");
                        }
                        sha.TransformFinalBlock(new byte[0], 0, 0);
                        if (!expected.SequenceEqual(sha.Hash)) throw InvalidPackage();
                    }
                    payload.Position = 0;
                    return payload;
                }
            }
            catch
            {
                file.Dispose();
                throw;
            }
        }

        // La entrada 4 del directorio PE32+ usa un offset de archivo, no una RVA.
        // SignTool agrega la tabla al final del instalador ya empaquetado.
        internal static long PayloadEnd(BinaryReader reader)
        {
            var file = reader.BaseStream;
            if (file.Length < 64 || reader.ReadUInt16() != 0x5a4d) throw InvalidPackage();
            file.Position = 0x3c;
            long pe = reader.ReadUInt32();
            if (pe < 64 || pe > file.Length - 24) throw InvalidPackage();
            file.Position = pe;
            if (reader.ReadUInt32() != 0x00004550 || reader.ReadUInt16() != 0x8664) throw InvalidPackage();
            file.Position = pe + 20;
            int optionalSize = reader.ReadUInt16();
            long optional = pe + 24;
            if (optionalSize < 152 || optionalSize > file.Length - optional) throw InvalidPackage();
            file.Position = optional;
            if (reader.ReadUInt16() != 0x20b) throw InvalidPackage();
            file.Position = optional + 108;
            if (reader.ReadUInt32() < 5) return file.Length;
            file.Position = optional + 144;
            long certificate = reader.ReadUInt32();
            long size = reader.ReadUInt32();
            if (certificate == 0 && size == 0) return file.Length;
            if (certificate < optional + optionalSize || certificate % 8 != 0 || size < 8
                || certificate + size != file.Length) throw InvalidPackage();
            return certificate;
        }

        private static InvalidDataException InvalidPackage()
        {
            return new InvalidDataException("El instalador está incompleto o dañado. Descargalo de nuevo desde ZUENA.");
        }

        private static void Extract(ZipArchive zip, string target, Action<int, string> progress, CancellationToken token)
        {
            long total = zip.Entries.Sum(entry => entry.Length);
            long written = 0;
            var drive = new DriveInfo(Path.GetPathRoot(target));
            if (drive.IsReady && drive.AvailableFreeSpace < total)
                throw new IOException("No hay espacio disponible para instalar ZUENA Plugins.");

            var buffer = new byte[128 * 1024];
            foreach (var entry in zip.Entries)
            {
                token.ThrowIfCancellationRequested();
                string name = entry.FullName;
                if (!name.StartsWith(Prefix, StringComparison.Ordinal)) throw InvalidPackage();
                string relative = name.Substring(Prefix.Length).TrimEnd('/');
                if (relative.Length == 0 && name == Prefix) continue;
                string[] parts = relative.Split('/');
                if (parts.Any(part => part.Length == 0 || part.EndsWith(".") || part.EndsWith(" ")
                    || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || IsDeviceName(part)))
                    throw InvalidPackage();

                string path = Path.GetFullPath(Path.Combine(target, Path.Combine(parts)));
                if (!path.StartsWith(target + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                    throw InvalidPackage();
                if (name.EndsWith("/", StringComparison.Ordinal))
                {
                    Directory.CreateDirectory(path);
                    continue;
                }
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                using (var input = entry.Open())
                using (var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write))
                {
                    int count;
                    long entryWritten = 0;
                    while ((count = input.Read(buffer, 0, buffer.Length)) > 0)
                    {
                        token.ThrowIfCancellationRequested();
                        entryWritten += count;
                        if (entryWritten > entry.Length) throw InvalidPackage();
                        output.Write(buffer, 0, count);
                        written += count;
                        progress(20 + (int)(74 * written / Math.Max(total, 1)), "Instalando ZUENA Plugins…");
                    }
                    if (entryWritten != entry.Length) throw InvalidPackage();
                }
            }
        }

        private static bool IsDeviceName(string part)
        {
            string stem = part.Split('.')[0].TrimEnd(' ').ToUpperInvariant();
            return stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL"
                || (stem.Length == 4 && (stem.StartsWith("COM") || stem.StartsWith("LPT"))
                    && "123456789¹²³".IndexOf(stem[3]) >= 0);
        }

        private static void ValidateLauncher(string staged)
        {
            if (!File.Exists(Path.Combine(staged, "resources", "app.asar"))
                || !File.Exists(Path.Combine(staged, Icon))
                || !File.Exists(Path.Combine(staged, Executable)))
                throw InvalidPackage();
            using (var reader = new BinaryReader(File.OpenRead(Path.Combine(staged, Executable))))
            {
                if (reader.BaseStream.Length < 64 || reader.ReadUInt16() != 0x5a4d) throw InvalidPackage();
                reader.BaseStream.Position = 0x3c;
                int offset = reader.ReadInt32();
                if (offset < 64 || offset > reader.BaseStream.Length - 6) throw InvalidPackage();
                reader.BaseStream.Position = offset;
                if (reader.ReadUInt32() != 0x00004550 || reader.ReadUInt16() != 0x8664) throw InvalidPackage();
            }
        }

        private static void DeleteDirectory(string path)
        {
            try { if (Directory.Exists(path)) Directory.Delete(path, true); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }

        private sealed class Shortcut
        {
            private readonly string path;
            private readonly string incoming;
            private readonly string backup;
            private bool committed;

            internal Shortcut(string path, string destination)
            {
                this.path = path;
                var temp = Path.Combine(Path.GetDirectoryName(path), ".zuena-" + Guid.NewGuid().ToString("N"));
                incoming = temp + ".lnk";
                backup = temp + ".bak";
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                try { Create(incoming, destination); }
                catch
                {
                    DeleteFile(incoming);
                    throw;
                }
            }

            private static void Create(string path, string destination)
            {
                var type = Type.GetTypeFromProgID("WScript.Shell", true);
                var shell = Activator.CreateInstance(type);
                try
                {
                    var link = type.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path });
                    try
                    {
                        var linkType = link.GetType();
                        linkType.InvokeMember("TargetPath", BindingFlags.SetProperty, null, link, new object[] { Path.Combine(destination, Executable) });
                        linkType.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, link, new object[] { destination });
                        linkType.InvokeMember("IconLocation", BindingFlags.SetProperty, null, link, new object[] { Path.Combine(destination, Icon) + ",0" });
                        linkType.InvokeMember("Description", BindingFlags.SetProperty, null, link, new object[] { Product });
                        linkType.InvokeMember("Save", BindingFlags.InvokeMethod, null, link, null);
                    }
                    finally { Marshal.ReleaseComObject(link); }
                }
                finally { Marshal.ReleaseComObject(shell); }
            }

            internal void Commit()
            {
                if (File.Exists(path)) File.Replace(incoming, path, backup, true);
                else File.Move(incoming, path);
                committed = true;
            }

            internal void Rollback()
            {
                if (File.Exists(backup)) File.Replace(backup, path, null, true);
                else if (committed) File.Delete(path);
            }

            internal void Clean(bool keepBackup)
            {
                DeleteFile(incoming);
                if (!keepBackup) DeleteFile(backup);
            }

            private static void DeleteFile(string path)
            {
                try { File.Delete(path); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
        }

        // ZipArchive ve el ZIP como un archivo independiente dentro del ejecutable.
        private sealed class PayloadStream : Stream
        {
            private readonly FileStream file;
            private readonly long start;
            private readonly long length;
            private long position;

            internal PayloadStream(FileStream file, long start, long length)
            {
                this.file = file;
                this.start = start;
                this.length = length;
                file.Position = start;
            }

            public override bool CanRead { get { return true; } }
            public override bool CanSeek { get { return true; } }
            public override bool CanWrite { get { return false; } }
            public override long Length { get { return length; } }
            public override long Position
            {
                get { return position; }
                set
                {
                    if (value < 0) throw new IOException("Posición inválida en el ZIP.");
                    file.Position = checked(start + value);
                    position = value;
                }
            }
            public override int Read(byte[] buffer, int offset, int count)
            {
                int read = file.Read(buffer, offset, (int)Math.Max(0, Math.Min(count, length - position)));
                position += read;
                return read;
            }
            public override long Seek(long offset, SeekOrigin origin)
            {
                Position = checked(offset + (origin == SeekOrigin.Begin ? 0 : origin == SeekOrigin.Current ? position : length));
                return position;
            }
            public override void Flush() { }
            public override void SetLength(long value) { throw new NotSupportedException(); }
            public override void Write(byte[] buffer, int offset, int count) { throw new NotSupportedException(); }
            protected override void Dispose(bool disposing)
            {
                if (disposing) file.Dispose();
                base.Dispose(disposing);
            }
        }
    }
}
