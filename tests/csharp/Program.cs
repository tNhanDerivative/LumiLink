using System.Diagnostics;
using LumiLink;

// Child mode: chay kich ban Raw va de native ghi byte ra stdout cua process nay.
if (args.Length > 0 && args[0] == "raw")
{
    LedClient.LogMode = LogMode.Silent;
    RunRawScenario();
    return 0;
}

int failed = 0;
void Check(string name, bool ok, string detail = "")
{
    Console.WriteLine($"[{(ok ? "PASS" : "FAIL")}] {name}{(ok || detail == "" ? "" : "  -> " + detail)}");
    if (!ok) failed++;
}
LumiLinkResult? ResultOf(Action a)
{
    try { a(); return null; }
    catch (LumiLinkException e) { return e.Result; }
}

// ---------------------------------------------------------------- 2. Smoke voi Text manager
Console.WriteLine("== 2. Smoke (Text manager, khong can den) ==");
{
    var logs = new List<string>();
    LedClient.SetLogHandler((lvl, msg) => { lock (logs) logs.Add($"{lvl}: {msg}"); });

    using var c = new LedClient();
    c.ChangeDeviceManager(DeviceManagerKind.Text);
    c.FindDevice();
    c.SetColor(LedClient.AllLeds, new LedColor(255, 0, 0));
    c.SetBrightness(0, 32);
    c.SetEffect(2, Effect.Blink, 10, new LedColor(0, 255, 0));
    c.NewGroup("ends"); c.AddToGroup("ends", 0); c.AddToGroup("ends", 7);
    c.SetGroupColor("ends", LedColor.White);
    c.SetGroupBrightness("ends", 100);
    c.SetGroupEffect("ends", Effect.Blink, 200, LedColor.White);
    c.TurnOffGroup("ends");
    c.RemoveFromGroup("ends", 7);
    c.TurnOff(LedClient.AllLeds);
    Check("chuoi lenh Text chay het, khong exception", true);
    Check("log callback nhan duoc log", logs.Count > 0, $"count={logs.Count}");
    Console.WriteLine($"   (vi du log: {logs.FirstOrDefault() ?? "<none>"})");
    Check("Describe(Ok) tra ve chuoi", LedClient.Describe(LumiLinkResult.Ok).Length > 0);
    LedClient.SetLogHandler(null);
    LedClient.LogMode = LogMode.Silent;
}

// ---------------------------------------------------------------- 3. Duong loi
Console.WriteLine("== 3. Duong loi ==");
{
    using var c = new LedClient();
    c.ChangeDeviceManager(DeviceManagerKind.Text);
    c.FindDevice();

    Check("AddToGroup nhom khong ton tai -> NoSuchGroup",
        ResultOf(() => c.AddToGroup("nope", 1)) == LumiLinkResult.NoSuchGroup);
    c.NewGroup("g");
    Check("NewGroup trung ten -> GroupExists",
        ResultOf(() => c.NewGroup("g")) == LumiLinkResult.GroupExists);
    c.AddToGroup("g", 1);
    Check("AddToGroup lan 2 -> LedAlreadyInGroup",
        ResultOf(() => c.AddToGroup("g", 1)) == LumiLinkResult.LedAlreadyInGroup);
    Check("RemoveFromGroup LED khong thuoc nhom -> LedNotInGroup",
        ResultOf(() => c.RemoveFromGroup("g", 9)) == LumiLinkResult.LedNotInGroup);
    Check("DeviceManagerKind ngoai pham vi -> InvalidArgument",
        ResultOf(() => c.ChangeDeviceManager((DeviceManagerKind)99)) == LumiLinkResult.InvalidArgument);
    Check("null string -> ArgumentNullException",
        Throws<ArgumentNullException>(() => c.NewGroup(null!)));
    Check("Write(null) -> ArgumentNullException",
        Throws<ArgumentNullException>(() => c.Write(null!)));
}
{
    var c = new LedClient();
    c.Dispose();
    bool noThrow = true;
    try { c.Dispose(); } catch { noThrow = false; }
    Check("Dispose hai lan khong nem exception", noThrow);
    Check("dung sau Dispose -> ObjectDisposedException (khong crash)",
        Throws<ObjectDisposedException>(() => c.SetColor(0, LedColor.White)));
}
{
    // Bluetooth voi PAL mock (build/): FindDevice phai thanh cong hoac tra ma loi ro rang, khong crash.
    using var c = new LedClient();
    c.ChangeDeviceManager(DeviceManagerKind.Bluetooth);
    var r = ResultOf(() => c.FindDevice());
    Console.WriteLine($"   Bluetooth FindDevice -> {(r?.ToString() ?? "Ok")}  (HasDevice={c.HasDevice})");
    Check("Bluetooth manager: ket qua la Ok hoac LumiLinkException", true);
}

static bool Throws<T>(Action a) where T : Exception
{
    try { a(); return false; }
    catch (T) { return true; }
}

// ---------------------------------------------------------------- 4. Log callback
Console.WriteLine("== 4. Log callback ==");
{
    int n1 = 0, n2 = 0;
    LedClient.SetLogHandler((_, _) => Interlocked.Increment(ref n1));
    using var c = new LedClient();
    c.ChangeDeviceManager(DeviceManagerKind.Text);
    c.FindDevice();
    c.SetColor(0, LedColor.White);
    Check("handler 1 nhan log", n1 > 0, $"n1={n1}");

    LedClient.SetLogHandler((_, _) => throw new InvalidOperationException("boom"));
    bool survived = true;
    try { c.SetColor(0, LedColor.White); } catch { survived = false; }
    Check("exception trong handler khong lan vao native", survived);

    LedClient.SetLogHandler(null);
    LedClient.LogMode = LogMode.Silent;
    int before = n1;
    c.SetColor(0, LedColor.White);
    Check("SetLogHandler(null) -> khong con log toi handler cu", n1 == before);

    LedClient.SetLogHandler((_, _) => Interlocked.Increment(ref n2));
    GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();   // delegate phai song sau GC
    c.SetColor(0, LedColor.White);
    Check("dang ky lai sau GC van nhan log", n2 > 0, $"n2={n2}");
    LedClient.SetLogHandler(null);
    LedClient.LogMode = LogMode.Silent;
}

// ---------------------------------------------------------------- 5. Stress
Console.WriteLine("== 5. Stress ==");
{
    LedClient.LogMode = LogMode.Silent;
    var sw = Stopwatch.StartNew();
    for (int i = 0; i < 10_000; i++)
    {
        using var c = new LedClient();
        c.ChangeDeviceManager(DeviceManagerKind.Text);
        c.SetColor(0, LedColor.White);
    }
    Check($"10.000 LedClient create/Dispose ({sw.ElapsedMilliseconds} ms)", true);

    // Khong Dispose: de finalizer cua SafeHandle thu hoi.
    for (int i = 0; i < 5_000; i++)
    {
        var c = new LedClient();
        c.ChangeDeviceManager(DeviceManagerKind.Text);
    }
    GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
    Check("5.000 LedClient bo cho finalizer, GC sach, khong crash", true);

    // 8 thread x 500 lenh tren cung mot client.
    using var shared = new LedClient();
    shared.ChangeDeviceManager(DeviceManagerKind.Text);
    var errors = new System.Collections.Concurrent.ConcurrentBag<Exception>();
    var threads = Enumerable.Range(0, 8).Select(t => new Thread(() =>
    {
        try
        {
            for (int i = 0; i < 500; i++)
            {
                shared.SetColor((byte)(i % 8), new LedColor((byte)t, (byte)i, 0));
                if (i % 100 == 0) shared.ChangeDeviceManager(DeviceManagerKind.Text);
            }
        }
        catch (Exception e) { errors.Add(e); }
    })).ToList();
    threads.ForEach(t => t.Start());
    threads.ForEach(t => t.Join());
    Check("8 thread x 500 lenh chung 1 client", errors.IsEmpty, errors.FirstOrDefault()?.Message ?? "");
}

// ---------------------------------------------------------------- 6. Raw == golden
Console.WriteLine("== 6. Raw vs golden raw_sequence.bin ==");
{
    string self = Environment.ProcessPath!;
    string dll = typeof(LedClient).Assembly.Location;
    var psi = new ProcessStartInfo(self) { RedirectStandardOutput = true, UseShellExecute = false };
    // Chay bang `dotnet Smoke.dll raw` hoac apphost `Smoke raw`.
    if (Path.GetFileNameWithoutExtension(self) == "dotnet")
    {
        psi.ArgumentList.Add(Path.Combine(AppContext.BaseDirectory, "Smoke.dll"));
    }
    psi.ArgumentList.Add("raw");
    using var p = Process.Start(psi)!;
    using var ms = new MemoryStream();
    p.StandardOutput.BaseStream.CopyTo(ms);
    p.WaitForExit();
    byte[] actual = ms.ToArray();
    byte[] expected = File.ReadAllBytes(Path.Combine(AppContext.BaseDirectory, "raw_sequence.bin"));
    Check("child process exit code 0", p.ExitCode == 0, $"exit={p.ExitCode}");
    Check($"do dai byte khop golden ({actual.Length} vs {expected.Length})", actual.Length == expected.Length);
    bool same = actual.AsSpan().SequenceEqual(expected);
    Check("tung byte khop golden", same);
    if (!same)
    {
        Console.WriteLine($"   expected: {Convert.ToHexString(expected)}");
        Console.WriteLine($"   actual:   {Convert.ToHexString(actual)}");
    }
}

Console.WriteLine(failed == 0 ? "\nALL PASS" : $"\n{failed} CHECK(S) FAILED");
return failed == 0 ? 0 : 1;

// Phai trung khop voi RunScenario() trong tests/core/GoldenRawTests.cpp
static void RunRawScenario()
{
    using var c = new LedClient();
    c.ChangeDeviceManager(DeviceManagerKind.Raw);
    c.FindDevice();
    c.SetColor(0, new LedColor(255, 0, 0));
    c.SetColor(LedClient.AllLeds, new LedColor(0, 0, 0));
    c.SetBrightness(1, 128);
    c.SetEffect(2, Effect.Blink, 10, new LedColor(0, 255, 0));
    c.SetEffect(LedClient.AllLeds, Effect.Rainbow, 200, default);
    c.NewGroup("pair");
    c.AddToGroup("pair", 3);
    c.AddToGroup("pair", 4);
    c.SetGroupEffect("pair", Effect.Breathe, 30, new LedColor(0, 0, 255));
    c.TurnOffGroup("pair");
    c.TurnOff(LedClient.AllLeds);
}
