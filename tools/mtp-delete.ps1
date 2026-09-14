<#
  Delete a file on the MTP device without the shell's confirmation dialog.

      . tools\mtp-delete.ps1
      Remove-MtpPath @('SD Card','switch','Jouster.nro')

  Why this exists: FolderItem.InvokeVerb('delete') always raises "are you sure
  you want to permanently delete this file?" and waits for a click. There is no
  flag to suppress it -- the verb runs the shell's own UI. That made every push
  of a new build need a human at the keyboard, which defeats a courier.

  IFileOperation does the same work with the UI off, but it takes IShellItems
  and the obvious way of getting one does not work here. An MTP item's shell
  path looks like

      ::{20D04FE0-...}\\?\usb#vid_057e&pid_201d#...\{00000026-0000-...}

  and SHCreateItemFromParsingName resolves the *device* fine and then fails
  with E_INVALIDARG on anything inside it -- measured, not assumed: the device
  parses, "SD Card" and "switch" both do not. Those trailing GUIDs are MTP
  object IDs, not parseable display names.

  So this parses the device only, and then walks down by enumerating children
  and matching names, which needs no parsing at all.
#>

if (-not ('Arkchemy.Mtp' -as [type])) {
Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace Arkchemy
{
    [ComImport, Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IShellItem
    {
        [PreserveSig] int BindToHandler(IntPtr pbc, ref Guid bhid, ref Guid riid, out IntPtr ppv);
        [PreserveSig] int GetParent(out IShellItem ppsi);
        [PreserveSig] int GetDisplayName(uint sigdnName, out IntPtr ppszName);
        [PreserveSig] int GetAttributes(uint sfgaoMask, out uint psfgaoAttribs);
        [PreserveSig] int Compare(IShellItem psi, uint hint, out int piOrder);
    }

    [ComImport, Guid("70629033-e363-4a28-a567-0db78006e6d7"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IEnumShellItems
    {
        [PreserveSig] int Next(uint celt, out IShellItem rgelt, out uint pceltFetched);
        [PreserveSig] int Skip(uint celt);
        [PreserveSig] int Reset();
        [PreserveSig] int Clone(out IEnumShellItems ppenum);
    }

    // Method order IS vtable order. Every entry must stay, in this sequence,
    // even the ones never called, or DeleteItem lands on the wrong slot.
    [ComImport, Guid("947aab5f-0a5c-4c13-b4d6-4bf7836fc9f8"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IFileOperation
    {
        void Advise(IntPtr pfops, out uint pdwCookie);
        void Unadvise(uint dwCookie);
        void SetOperationFlags(uint dwOperationFlags);
        void SetProgressMessage([MarshalAs(UnmanagedType.LPWStr)] string pszMessage);
        void SetProgressDialog(IntPtr popd);
        void SetProperties(IntPtr pproparray);
        void SetOwnerWindow(IntPtr hwndOwner);
        void ApplyPropertiesToItem(IShellItem psiItem);
        void ApplyPropertiesToItems(object punkItems);
        void RenameItem(IShellItem psiItem, [MarshalAs(UnmanagedType.LPWStr)] string pszNewName, IntPtr pfopsItem);
        void RenameItems(object pUnkItems, [MarshalAs(UnmanagedType.LPWStr)] string pszNewName);
        void MoveItem(IShellItem psiItem, IShellItem psiDestinationFolder, [MarshalAs(UnmanagedType.LPWStr)] string pszNewName, IntPtr pfopsItem);
        void MoveItems(object punkItems, IShellItem psiDestinationFolder);
        void CopyItem(IShellItem psiItem, IShellItem psiDestinationFolder, [MarshalAs(UnmanagedType.LPWStr)] string pszCopyName, IntPtr pfopsItem);
        void CopyItems(object punkItems, IShellItem psiDestinationFolder);
        void DeleteItem(IShellItem psiItem, IntPtr pfopsItem);
        void DeleteItems(object punkItems);
        void NewItem(IShellItem psiDestinationFolder, uint dwFileAttributes, [MarshalAs(UnmanagedType.LPWStr)] string pszName, [MarshalAs(UnmanagedType.LPWStr)] string pszTemplateName, IntPtr pfopsItem);
        void PerformOperations();
        void GetAnyOperationsAborted(out bool pfAnyOperationsAborted);
    }

    [ComImport, Guid("3ad05575-8857-4850-9277-11b85bdb8e09")]
    public class FileOperation { }

    public static class Mtp
    {
        [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
        static extern void SHCreateItemFromParsingName(
            [MarshalAs(UnmanagedType.LPWStr)] string pszPath, IntPtr pbc, ref Guid riid,
            [MarshalAs(UnmanagedType.Interface)] out IShellItem ppv);

        static readonly Guid IID_IShellItem     = new Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe");
        static readonly Guid BHID_EnumItems     = new Guid("94f60519-2850-4924-aa5a-d15e84868039");
        static readonly Guid IID_IEnumShellItems= new Guid("70629033-e363-4a28-a567-0db78006e6d7");

        // FOF_SILENT 0x4 | FOF_NOCONFIRMATION 0x10 | FOF_NOCONFIRMMKDIR 0x200
        //   | FOF_NOERRORUI 0x400 -- the whole point: no dialog, for anything.
        const uint FOF_NO_UI = 0x4 | 0x10 | 0x200 | 0x400;
        const uint SIGDN_NORMALDISPLAY = 0;

        static string NameOf(IShellItem it)
        {
            IntPtr p;
            if (it.GetDisplayName(SIGDN_NORMALDISPLAY, out p) != 0 || p == IntPtr.Zero) return null;
            try { return Marshal.PtrToStringUni(p); } finally { Marshal.FreeCoTaskMem(p); }
        }

        /// Find a direct child by display name. Returns null if absent.
        static IShellItem Child(IShellItem parent, string name)
        {
            Guid bhid = BHID_EnumItems, iid = IID_IEnumShellItems;
            IntPtr raw;
            if (parent.BindToHandler(IntPtr.Zero, ref bhid, ref iid, out raw) != 0 || raw == IntPtr.Zero)
                return null;
            IEnumShellItems e = (IEnumShellItems)Marshal.GetObjectForIUnknown(raw);
            Marshal.Release(raw);
            try
            {
                IShellItem child; uint got;
                while (e.Next(1, out child, out got) == 0 && got == 1)
                {
                    if (string.Equals(NameOf(child), name, StringComparison.OrdinalIgnoreCase))
                        return child;
                    Marshal.ReleaseComObject(child);
                }
            }
            finally { Marshal.ReleaseComObject(e); }
            return null;
        }

        /// Resolve deviceParsingName + segments, then delete the last one.
        /// Throws with the segment that could not be found, which is far more
        /// use than a bare E_INVALIDARG when a card layout changes.
        public static void DeletePath(string deviceParsingName, string[] segments)
        {
            IShellItem cur;
            Guid iid = IID_IShellItem;
            SHCreateItemFromParsingName(deviceParsingName, IntPtr.Zero, ref iid, out cur);

            for (int i = 0; i < segments.Length; i++)
            {
                IShellItem next = Child(cur, segments[i]);
                Marshal.ReleaseComObject(cur);
                if (next == null)
                    throw new Exception("not found on the device: " + string.Join("/", segments, 0, i + 1));
                cur = next;
            }

            IFileOperation op = (IFileOperation)new FileOperation();
            try
            {
                op.SetOperationFlags(FOF_NO_UI);
                op.SetOwnerWindow(IntPtr.Zero);
                op.DeleteItem(cur, IntPtr.Zero);
                op.PerformOperations();
                bool aborted;
                op.GetAnyOperationsAborted(out aborted);
                if (aborted) throw new Exception("the shell reported the delete was aborted");
            }
            finally { Marshal.ReleaseComObject(op); Marshal.ReleaseComObject(cur); }
        }
    }
}
'@
}

<#
  Delete one path under the connected Switch, silently.

  $Segments is the path below the device, e.g. @('SD Card','switch','Jouster.nro').
  Returns $true if the file is gone afterwards.

  The confirmation at the end is deliberate: IFileOperation can report success
  for an operation the device then declines, and a push that believes a stale
  NRO was removed copies alongside it as "Jouster (2).nro", leaving the console
  booting the old build.
#>
function Remove-MtpPath {
    param(
        [Parameter(Mandatory = $true)] [string[]]$Segments,
        [string]$DeviceName = 'Nintendo Switch'
    )

    $shell = New-Object -ComObject Shell.Application
    $dev = $shell.NameSpace(17).Items() | Where-Object { $_.Name -eq $DeviceName }
    if (-not $dev) { throw "no '$DeviceName' under This PC" }

    [Arkchemy.Mtp]::DeletePath($dev.Path, $Segments)

    # Verify through the Shell namespace rather than trusting the operation.
    $folder = $dev.GetFolder
    for ($i = 0; $i -lt $Segments.Length - 1; $i++) {
        $folder = ($folder.Items() | Where-Object { $_.Name -eq $Segments[$i] }).GetFolder
        if (-not $folder) { return $true }
    }
    $leaf = $Segments[-1]
    for ($i = 0; $i -lt 40; $i++) {
        if (-not ($folder.Items() | Where-Object { $_.Name -eq $leaf })) { return $true }
        Start-Sleep -Milliseconds 400
    }
    return $false
}
