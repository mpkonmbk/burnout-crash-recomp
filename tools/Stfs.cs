using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.Security.Cryptography;

// Minimal read-only STFS (LIVE/PIRS/CON) extractor with full hash-tree verification.
// Block mapping follows Velocity / Xenia (C# 5 syntax for PowerShell 5.1 Add-Type).
public class StfsExtractor
{
    const int BlockSize = 0x1000;
    const int PerLevel = 0xAA;      // 170
    const int PerLevel1 = 0x70E4;   // 28900

    FileStream fs;
    long firstBlockOffset;
    int sex;            // 0 = read-only ("female"), 1 = writable ("male")
    long[] blockStep;
    public uint TotalBlocks;
    public int FileTableBlockCount;
    public uint FileTableBlockNum;
    byte[] topHash;
    public StringBuilder Log = new StringBuilder();

    public class Entry
    {
        public string Name;
        public bool IsDir;
        public bool Consecutive;
        public uint ValidBlocks;
        public uint StartBlock;
        public int Parent;
        public uint Size;
        public string Path;
    }
    public List<Entry> Entries = new List<Entry>();

    static uint BE32(byte[] b, int o) { return ((uint)b[o] << 24) | ((uint)b[o + 1] << 16) | ((uint)b[o + 2] << 8) | b[o + 3]; }
    static uint BE24(byte[] b, int o) { return ((uint)b[o] << 16) | ((uint)b[o + 1] << 8) | b[o + 2]; }
    static uint LE24(byte[] b, int o) { return (uint)b[o] | ((uint)b[o + 1] << 8) | ((uint)b[o + 2] << 16); }

    public StfsExtractor(string path)
    {
        fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20);
        byte[] h = ReadRaw(0, 0x1000);
        string magic = Encoding.ASCII.GetString(h, 0, 4);
        if (magic != "LIVE" && magic != "PIRS" && magic != "CON ") throw new Exception("Not an STFS package: " + magic);
        uint headerSize = BE32(h, 0x340);
        firstBlockOffset = ((long)headerSize + 0xFFF) & ~0xFFFL;
        if (BE32(h, 0x3A9) != 0) throw new Exception("Descriptor type is not STFS (SVOD not supported)");
        byte flags = h[0x37B];
        sex = (~flags) & 1;
        blockStep = sex == 0 ? new long[] { 0xAB, 0x718F } : new long[] { 0xAC, 0x723A };
        FileTableBlockCount = h[0x37C] | (h[0x37D] << 8);
        FileTableBlockNum = LE24(h, 0x37E);
        topHash = new byte[20];
        Array.Copy(h, 0x381, topHash, 0, 20);
        TotalBlocks = BE32(h, 0x395);
        Log.AppendLine(string.Format("Magic={0} HeaderSize=0x{1:X} FirstBlock=0x{2:X} Flags=0x{3:X2} ReadOnly={4} FileTableBlocks={5} FileTableStart={6} TotalBlocks={7}",
            magic, headerSize, firstBlockOffset, flags, sex == 0, FileTableBlockCount, FileTableBlockNum, TotalBlocks));
    }

    public void Close() { fs.Close(); }

    byte[] ReadRaw(long offset, int len)
    {
        byte[] buf = new byte[len];
        fs.Position = offset;
        int read = 0;
        while (read < len)
        {
            int n = fs.Read(buf, read, len - read);
            if (n <= 0) throw new EndOfStreamException("Unexpected EOF at 0x" + (offset + read).ToString("X"));
            read += n;
        }
        return buf;
    }

    long BackingData(uint b)
    {
        long r = (((long)(b + PerLevel) / PerLevel) << sex) + b;
        if (b < PerLevel) return r;
        if (b < PerLevel1) return r + (((long)(b + PerLevel1) / PerLevel1) << sex);
        return (1L << sex) + r + (((long)(b + PerLevel1) / PerLevel1) << sex);
    }

    long BackingHash(uint b, int level)
    {
        if (level == 0)
        {
            if (b < PerLevel) return 0;
            long num = (b / PerLevel) * blockStep[0];
            num += ((long)(b / PerLevel1) + 1) << sex;
            if (b / PerLevel1 == 0) return num;
            return num + (1L << sex);
        }
        if (level == 1)
        {
            if (b < PerLevel1) return blockStep[0];
            return (1L << sex) + (b / PerLevel1) * blockStep[1];
        }
        return blockStep[1];
    }

    long Off(long backing) { return firstBlockOffset + backing * BlockSize; }

    byte[] ReadDataBlock(uint b) { return ReadRaw(Off(BackingData(b)), BlockSize); }

    byte[] HashEntry(uint b, int level)
    {
        int idx;
        if (level == 0) idx = (int)(b % PerLevel);
        else if (level == 1) idx = (int)((b / PerLevel) % PerLevel);
        else idx = (int)((b / PerLevel1) % PerLevel);
        return ReadRaw(Off(BackingHash(b, level)) + idx * 0x18, 0x18);
    }

    uint NextBlock(uint b) { return BE24(HashEntry(b, 0), 0x15); }

    static bool Eq(byte[] a, int ao, byte[] b, int bo, int n)
    {
        for (int i = 0; i < n; i++) if (a[ao + i] != b[bo + i]) return false;
        return true;
    }

    // Verifies every data block and every hash table up to the top hash in the header.
    public bool VerifyAll()
    {
        SHA1 sha = SHA1.Create();
        int bad = 0;
        // Level 0: data blocks
        byte[] l0 = null; long l0Backing = -1;
        for (uint b = 0; b < TotalBlocks; b++)
        {
            long hb = BackingHash(b, 0);
            if (hb != l0Backing) { l0 = ReadRaw(Off(hb), BlockSize); l0Backing = hb; }
            byte[] d = ReadDataBlock(b);
            byte[] hsh = sha.ComputeHash(d);
            if (!Eq(hsh, 0, l0, (int)(b % PerLevel) * 0x18, 20))
            {
                if (bad < 20) Log.AppendLine("Data block hash mismatch: " + b);
                bad++;
            }
        }
        int levels = TotalBlocks <= PerLevel ? 0 : (TotalBlocks <= PerLevel1 ? 1 : 2);
        // Level 0 tables -> level 1 entries, level 1 tables -> level 2 entries
        for (int lvl = 0; lvl < levels; lvl++)
        {
            uint span = lvl == 0 ? (uint)PerLevel : (uint)PerLevel1;
            for (uint b = 0; b < TotalBlocks; b += span)
            {
                byte[] table = ReadRaw(Off(BackingHash(b, lvl)), BlockSize);
                byte[] hsh = sha.ComputeHash(table);
                byte[] parent = HashEntry(b, lvl + 1);
                if (!Eq(hsh, 0, parent, 0, 20))
                {
                    if (bad < 40) Log.AppendLine("Level " + lvl + " table hash mismatch at block " + b);
                    bad++;
                }
            }
        }
        byte[] top = ReadRaw(Off(BackingHash(0, levels)), BlockSize);
        if (!Eq(sha.ComputeHash(top), 0, topHash, 0, 20)) { Log.AppendLine("Top hash mismatch"); bad++; }
        Log.AppendLine("Verification: " + (bad == 0 ? "OK (all " + TotalBlocks + " data blocks + hash tree match)" : bad + " mismatches"));
        return bad == 0;
    }

    // Returns the list of data blocks whose SHA1 doesn't match their level-0 entry, and whether each is all zeros.
    public List<uint> BadBlocks = new List<uint>();
    public List<bool> BadBlockZero = new List<bool>();
    public void FindBadBlocks()
    {
        SHA1 sha = SHA1.Create();
        byte[] l0 = null; long l0Backing = -1;
        for (uint b = 0; b < TotalBlocks; b++)
        {
            long hb = BackingHash(b, 0);
            if (hb != l0Backing) { l0 = ReadRaw(Off(hb), BlockSize); l0Backing = hb; }
            byte[] d = ReadDataBlock(b);
            if (!Eq(sha.ComputeHash(d), 0, l0, (int)(b % PerLevel) * 0x18, 20))
            {
                bool zero = true;
                foreach (byte x in d) if (x != 0) { zero = false; break; }
                BadBlocks.Add(b); BadBlockZero.Add(zero);
            }
        }
    }

    public long BackingOffsetOf(uint b) { return Off(BackingData(b)); }

    // Data blocks belonging to a file (follows chain for non-consecutive files).
    public List<uint> BlocksOf(Entry e)
    {
        List<uint> r = new List<uint>();
        if (e.IsDir || e.Size == 0) return r;
        long n = ((long)e.Size + BlockSize - 1) / BlockSize;
        uint blk = e.StartBlock;
        for (long i = 0; i < n; i++)
        {
            r.Add(blk);
            if (i + 1 < n) blk = e.Consecutive ? blk + 1 : NextBlock(blk);
        }
        return r;
    }

    static bool SafeName(string n)
    {
        if (n.Length == 0 || n == "." || n == "..") return false;
        foreach (char c in n) if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
        return true;
    }

    public void ReadFileTable()
    {
        uint blk = FileTableBlockNum;
        for (int i = 0; i < FileTableBlockCount; i++)
        {
            byte[] d = ReadDataBlock(blk);
            for (int e = 0; e < 64; e++)
            {
                int o = e * 0x40;
                int nameLen = d[o + 0x28] & 0x3F;
                if (nameLen == 0) continue;
                Entry en = new Entry();
                en.Name = Encoding.ASCII.GetString(d, o, Math.Min(nameLen, 40));
                en.IsDir = (d[o + 0x28] & 0x80) != 0;
                en.Consecutive = (d[o + 0x28] & 0x40) != 0;
                en.ValidBlocks = LE24(d, o + 0x29);
                en.StartBlock = LE24(d, o + 0x2F);
                en.Parent = (short)((d[o + 0x32] << 8) | d[o + 0x33]);
                en.Size = BE32(d, o + 0x34);
                if (!SafeName(en.Name)) throw new Exception("Unsafe file name in package: " + en.Name);
                Entries.Add(en);
            }
            if (i + 1 < FileTableBlockCount) blk = NextBlock(blk);
        }
        for (int i = 0; i < Entries.Count; i++) Entries[i].Path = BuildPath(i, 0);
    }

    string BuildPath(int i, int depth)
    {
        if (depth > 64) throw new Exception("Directory cycle in file table");
        Entry e = Entries[i];
        if (e.Parent == -1) return e.Name;
        if (e.Parent < 0 || e.Parent >= Entries.Count) throw new Exception("Bad parent index for " + e.Name);
        return BuildPath(e.Parent, depth + 1) + "\\" + e.Name;
    }

    public void ExtractAll(string outDir)
    {
        string root = Path.GetFullPath(outDir);
        Directory.CreateDirectory(root);
        foreach (Entry e in Entries)
        {
            string target = Path.GetFullPath(Path.Combine(root, e.Path));
            if (!target.StartsWith(root + "\\", StringComparison.OrdinalIgnoreCase)) throw new Exception("Path escapes output dir: " + e.Path);
            if (e.IsDir) { Directory.CreateDirectory(target); continue; }
            Directory.CreateDirectory(Path.GetDirectoryName(target));
            using (FileStream o = new FileStream(target, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 20))
            {
                long remaining = e.Size;
                uint blk = e.StartBlock;
                uint count = 0;
                while (remaining > 0)
                {
                    byte[] d = ReadDataBlock(blk);
                    int n = (int)Math.Min(remaining, BlockSize);
                    o.Write(d, 0, n);
                    remaining -= n;
                    count++;
                    if (remaining > 0) blk = e.Consecutive ? blk + 1 : NextBlock(blk);
                }
            }
        }
    }
}
