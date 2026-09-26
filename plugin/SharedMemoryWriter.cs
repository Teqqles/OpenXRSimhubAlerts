// plugin/SharedMemoryWriter.cs
using System;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using OpenXRSimHubAlerts.Shared;

namespace OpenXRSimHubAlerts.Plugin {
  public sealed class SharedMemoryWriter : IDisposable {
    static readonly int Size = Marshal.SizeOf<DataBlock>();
    readonly MemoryMappedFile _mmf;
    readonly MemoryMappedViewAccessor _view;
    uint _seq;

    // name: tests pass their own so they never touch the live mapping.
    public SharedMemoryWriter(string name = ShmContract.Name) {
      _mmf = MemoryMappedFile.CreateOrOpen(name, Size);
      _view = _mmf.CreateViewAccessor(0, Size);
    }

    public void Write(ref DataBlock block) {
      block.Version = ShmContract.Version;
      _seq++;                                   // odd: write in progress
      block.Seq = _seq | 1u;

      // Marshal struct to unmanaged memory
      var ptr = Marshal.AllocHGlobal(Size);
      try {
        Marshal.StructureToPtr(block, ptr, false);
        _view.WriteArray(0, ReadBytes(ptr, Size), 0, Size);
        _view.Flush();

        _seq = (_seq | 1u) + 1u;                  // even: done
        block.Seq = _seq;
        // Write just the Seq field at offset 4
        var seqBytes = BitConverter.GetBytes(_seq);
        _view.WriteArray(4, seqBytes, 0, 4);
        _view.Flush();                            // ensure even-Seq ordering
      } finally {
        Marshal.FreeHGlobal(ptr);
      }
    }

    static byte[] ReadBytes(IntPtr ptr, int count) {
      var bytes = new byte[count];
      Marshal.Copy(ptr, bytes, 0, count);
      return bytes;
    }

    public static bool TryReadRaw(out DataBlock block, string name = ShmContract.Name) {
      block = default;
      try {
        using var mmf = MemoryMappedFile.OpenExisting(name);
        using var v = mmf.CreateViewAccessor(0, Size);

        var bytes = new byte[Size];
        v.ReadArray(0, bytes, 0, Size);
        var ptr = Marshal.AllocHGlobal(Size);
        try {
          Marshal.Copy(bytes, 0, ptr, Size);
          block = Marshal.PtrToStructure<DataBlock>(ptr);
        } finally {
          Marshal.FreeHGlobal(ptr);
        }
        return true;
      } catch {
        return false;
      }
    }

    public void Dispose() { _view.Dispose(); _mmf.Dispose(); }
  }
}
