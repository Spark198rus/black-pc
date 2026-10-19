# Create a Windows shortcut that also stores a path relative to the shortcut (IShellLink::SetRelativePath),
# so it still finds its target after the folder is moved or the drive letter changes.
# Usage: make_shortcut.ps1 -Link BLACK.lnk -Target work\run\black.exe -Arguments "..." [-Description "..."]
param([Parameter(Mandatory)][string]$Link, [Parameter(Mandatory)][string]$Target, [string]$Arguments = "", [string]$Description = "")

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;

[ComImport, Guid("000214F9-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IShellLinkW {
    void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder f, int cch, IntPtr pfd, uint flags);
    void GetIDList(out IntPtr ppidl);
    void SetIDList(IntPtr pidl);
    void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int cch);
    void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string s);
    void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int cch);
    void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string s);
    void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int cch);
    void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string s);
    void GetHotkey(out short h);
    void SetHotkey(short h);
    void GetShowCmd(out int c);
    void SetShowCmd(int c);
    void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int cch, out int i);
    void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string s, int i);
    void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string s, uint reserved);
    void Resolve(IntPtr hwnd, uint flags);
    void SetPath([MarshalAs(UnmanagedType.LPWStr)] string s);
}

[ComImport, Guid("00021401-0000-0000-C000-000000000046")]
class ShellLinkCoClass {}

public static class RelativeShortcut {
    public static void Create(string link, string target, string args, string desc) {
        var l = (IShellLinkW)new ShellLinkCoClass();
        l.SetPath(target);
        l.SetArguments(args);
        l.SetDescription(desc);
        l.SetWorkingDirectory("");
        l.SetRelativePath(link, 0); // store the target relative to the link file
        ((IPersistFile)l).Save(link, true);
    }
}
'@

$linkFull = [System.IO.Path]::GetFullPath($Link)
$targetFull = [System.IO.Path]::GetFullPath($Target)
[RelativeShortcut]::Create($linkFull, $targetFull, $Arguments, $Description)
"created $linkFull -> $targetFull $Arguments"
