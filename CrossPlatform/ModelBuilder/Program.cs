using Nintendo.Bfres;

const string ModelName = "Jugador1ModelNameLongForASpecificReason";

if (args.Length == 2 && args[0] == "--inspect")
{
    InspectModel(Path.GetFullPath(args[1]));
    return 0;
}

if (args.Length == 2 && args[0] == "--patch-event-flow")
{
    try
    {
        PatchTitleArchive(Path.GetFullPath(args[1]));
        return 0;
    }
    catch (Exception exception)
    {
        Console.Error.WriteLine(exception);
        return 1;
    }
}

if (args.Length != 3)
{
    Console.Error.WriteLine("Usage: milkbar-model-builder BASE_CONTENT UPDATE_CONTENT OUTPUT_CONTENT");
    Console.Error.WriteLine("       milkbar-model-builder --patch-event-flow TITLE_BG_PACK");
    return 2;
}

try
{
    BuildModels(Path.GetFullPath(args[0]), Path.GetFullPath(args[1]), Path.GetFullPath(args[2]));
    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine(exception);
    return 1;
}

static void InspectModel(string path)
{
    BfresFile file = Open(File.ReadAllBytes(path));
    foreach (Model model in file.Models.Values)
    {
        Console.WriteLine($"MODEL {model.Name}");
        Console.WriteLine("  SHAPES " + string.Join(" | ", model.Shapes.Keys));
        Console.WriteLine("  MATERIALS " + string.Join(" | ", model.Materials.Keys));
        Console.WriteLine("  BONES " + string.Join(" | ", model.Skeleton.Bones.Keys));
    }
}

static void BuildModels(string baseContent, string updateContent, string outputContent)
{
    string titlePath = Path.Combine(updateContent, "Pack", "TitleBG.pack");
    byte[] title = File.ReadAllBytes(titlePath);
    byte[] linkModel = ReadSarcEntry(title, "Model/Link.sbfres");
    byte[] linkTex2 = ReadSarcEntry(title, "Model/Link.Tex2.sbfres");
    byte[] defaultArmor = ReadSarcEntry(title, "Model/Armor_Default.sbfres");
    byte[] defaultArmorTex2 = ReadSarcEntry(title, "Model/Armor_Default.Tex2.sbfres");

    string modelDirectory = Path.Combine(outputContent, "Model");
    Directory.CreateDirectory(modelDirectory);

    BfresFile outputModel = Open(linkModel);
    outputModel.Models.Clear();
    AddLinkModel(outputModel, linkModel, $"{ModelName}Head", shape => !shape.Contains("Skin", StringComparison.Ordinal));
    AddLinkModel(outputModel, linkModel, $"{ModelName}Chest",
        shape => shape.Contains("Skin", StringComparison.Ordinal) && shape.Contains("Upper", StringComparison.Ordinal));
    AddLinkModel(outputModel, linkModel, "EmptyModel", _ => false);

    foreach (string path in Directory.EnumerateFiles(Path.Combine(updateContent, "Model"), "Armor_*.sbfres"))
    {
        if (Path.GetFileName(path).Contains("Tex", StringComparison.OrdinalIgnoreCase))
            continue;
        AddArmorModels(outputModel, Open(File.ReadAllBytes(path)));
    }
    AddArmorModels(outputModel, Open(defaultArmor));
    AddDefaultAlias(outputModel, defaultArmor, "Head", $"{ModelName}Helmet");
    AddDefaultAlias(outputModel, defaultArmor, "Lower", $"{ModelName}Lower");
    AddDefaultAlias(outputModel, defaultArmor, "Upper", $"{ModelName}Upper");
    outputModel.Name = ModelName;
    ValidateModels(outputModel);
    SaveCompressed(outputModel, Path.Combine(modelDirectory, $"{ModelName}.sbfres"));

    BfresFile outputTex1 = Open(File.ReadAllBytes(Path.Combine(baseContent, "Model", "Link.Tex1.sbfres")));
    foreach (string path in EnumerateTextureFiles(baseContent, "Tex1"))
        AddTextures(outputTex1, Open(File.ReadAllBytes(path)));
    foreach (string path in EnumerateTextureFiles(updateContent, "Tex1"))
        AddTextures(outputTex1, Open(File.ReadAllBytes(path)));
    outputTex1.Name = ModelName;
    SaveCompressed(outputTex1, Path.Combine(modelDirectory, $"{ModelName}.Tex1.sbfres"));

    BfresFile outputTex2 = Open(linkTex2);
    foreach (string path in EnumerateTextureFiles(updateContent, "Tex2"))
        AddTextures(outputTex2, Open(File.ReadAllBytes(path)));
    AddTextures(outputTex2, Open(defaultArmorTex2));
    outputTex2.Name = ModelName;
    SaveCompressed(outputTex2, Path.Combine(modelDirectory, $"{ModelName}.Tex2.sbfres"));

    BuildNoFaceAnimations(baseContent, modelDirectory);
    RestoreLocalLinkActors(baseContent, updateContent, outputContent);
    PatchMultiplayerEventFlow(outputContent);

    Console.WriteLine($"Created {ModelName} with {outputModel.Models.Count} models, " +
                      $"{outputTex1.Textures.Count} Tex1 textures and {outputTex2.Textures.Count} Tex2 textures.");
}

static void RestoreLocalLinkActors(string baseContent, string updateContent, string outputContent)
{
    string outputActors = Path.Combine(outputContent, "Actor", "Pack");
    foreach (string outputPath in Directory.EnumerateFiles(outputActors, "Armor_*.sbactorpack"))
    {
        string fileName = Path.GetFileName(outputPath);
        string updatePath = Path.Combine(updateContent, "Actor", "Pack", fileName);
        string basePath = Path.Combine(baseContent, "Actor", "Pack", fileName);
        string? source = File.Exists(updatePath) ? updatePath : (File.Exists(basePath) ? basePath : null);
        if (source is not null)
            File.Copy(source, outputPath, true);
    }

    string outputPause = Path.Combine(outputActors, "PauseMenuPlayer.sbactorpack");
    string updatePause = Path.Combine(updateContent, "Actor", "Pack", "PauseMenuPlayer.sbactorpack");
    string basePause = Path.Combine(baseContent, "Actor", "Pack", "PauseMenuPlayer.sbactorpack");
    string? pauseSource = File.Exists(updatePause) ? updatePause : (File.Exists(basePause) ? basePause : null);
    if (File.Exists(outputPause) && pauseSource is not null)
        File.Copy(pauseSource, outputPause, true);
}

static void PatchMultiplayerEventFlow(string outputContent)
{
    string titlePath = Path.Combine(outputContent, "Pack", "TitleBG.pack");
    PatchTitleArchive(titlePath);
}

static void PatchTitleArchive(string titlePath)
{
    byte[] title = File.ReadAllBytes(titlePath);
    if (!title.AsSpan(0, Math.Min(4, title.Length)).SequenceEqual("SARC"u8))
        throw new InvalidDataException("UKMM's TitleBG.pack must be an uncompressed SARC archive");

    (int start, int end) = FindSarcEntryRange(title, "EventFlow/MultiplayerEvent.bfevfl");
    int patched = PatchRemoteDeleteStatus(title.AsSpan(start, end - start));
    if (patched != 32)
        throw new InvalidDataException($"Expected 32 remote-player delete checks, patched {patched}");
    File.WriteAllBytes(titlePath, title);
    Console.WriteLine("Patched 32 remote-player EventFlow delete checks without rebuilding TitleBG.pack.");
}

static int PatchRemoteDeleteStatus(Span<byte> eventFlow)
{
    if (eventFlow.Length < 0x38 || !eventFlow[..8].SequenceEqual("BFEVFL\0\0"u8))
        throw new InvalidDataException("MultiplayerEvent is not a BFEVFL archive");

    int flowchartPointerArray = ReadOffset(eventFlow, 0x28);
    int flowchart = ReadOffset(eventFlow, flowchartPointerArray);
    RequireRange(eventFlow, flowchart, 0x38);
    if (!eventFlow.Slice(flowchart, 4).SequenceEqual("EVFL"u8))
        throw new InvalidDataException("MultiplayerEvent has no EVFL flowchart");

    int eventCount = Read16LE(eventFlow, flowchart + 0x16);
    int events = ReadOffset(eventFlow, flowchart + 0x30);
    RequireRange(eventFlow, events, checked(eventCount * 0x28));
    int patched = 0;

    for (int index = 0; index < eventCount; index++)
    {
        int eventOffset = events + index * 0x28;
        if (eventFlow[eventOffset + 8] != 1) // SwitchEvent
            continue;

        int parameters = ReadOffset(eventFlow, eventOffset + 0x10);
        if (parameters == 0)
            continue;
        RequireRange(eventFlow, parameters, 0x10);
        int itemCount = Read16LE(eventFlow, parameters + 2);
        RequireRange(eventFlow, parameters + 0x10, checked(itemCount * 8));
        bool remoteStatusCheck = false;
        int valueOffset = -1;
        int value = 0;

        for (int itemIndex = 0; itemIndex < itemCount; itemIndex++)
        {
            int item = ReadOffset(eventFlow, parameters + 0x10 + itemIndex * 8);
            RequireRange(eventFlow, item, 0x14);
            byte type = eventFlow[item];
            if (type == 2) // Int
            {
                valueOffset = item + 0x10;
                value = Read32LE(eventFlow, valueOffset);
            }
            else if (type == 5) // String
            {
                int stringOffset = ReadOffset(eventFlow, item + 0x10);
                RequireRange(eventFlow, stringOffset, 2);
                int length = Read16LE(eventFlow, stringOffset);
                RequireRange(eventFlow, stringOffset + 2, length);
                string text = System.Text.Encoding.UTF8.GetString(eventFlow.Slice(stringOffset + 2, length));
                if (text.StartsWith("Jugador", StringComparison.Ordinal) &&
                    text.EndsWith("_Status", StringComparison.Ordinal))
                    remoteStatusCheck = true;
            }
        }

        // Status 1 is the BNP's saved/default value and can be observed during
        // actor startup. Reserve 3 for an explicit native-client delete command
        // so initialization can never run MultiplayerAI's SystemDelete action.
        if (remoteStatusCheck && valueOffset >= 0 && value == 1)
        {
            System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(
                eventFlow.Slice(valueOffset, 4), 3);
            patched++;
        }
    }
    return patched;
}

static int ReadOffset(ReadOnlySpan<byte> data, int offset)
{
    RequireRange(data, offset, 8);
    ulong value = System.Buffers.Binary.BinaryPrimitives.ReadUInt64LittleEndian(data.Slice(offset, 8));
    return checked((int)value);
}

static int Read16LE(ReadOnlySpan<byte> data, int offset)
{
    RequireRange(data, offset, 2);
    return System.Buffers.Binary.BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(offset, 2));
}

static int Read32LE(ReadOnlySpan<byte> data, int offset)
{
    RequireRange(data, offset, 4);
    return System.Buffers.Binary.BinaryPrimitives.ReadInt32LittleEndian(data.Slice(offset, 4));
}

static void RequireRange(ReadOnlySpan<byte> data, int offset, int length)
{
    if (offset < 0 || length < 0 || offset > data.Length - length)
        throw new InvalidDataException("MultiplayerEvent contains an invalid offset");
}

static void BuildNoFaceAnimations(string baseContent, string modelDirectory)
{
    BfresFile animations = Open(File.ReadAllBytes(Path.Combine(baseContent, "Model", "Player_Animation.sbfres")));
    animations.Name = "Player_Animation_NoFace";
    string[] bannedBones =
    {
        "face", "cheek", "chin", "lip", "teeth", "eye", "eyeball",
        "eyebrow", "eyelid", "nose",
    };
    foreach (SkeletalAnim animation in animations.SkeletalAnims.Values)
    {
        animation.BoneAnims = animation.BoneAnims
            .Where(bone => !bannedBones.Any(banned =>
                bone.Name.Contains(banned, StringComparison.OrdinalIgnoreCase)))
            .ToList();
    }
    SaveCompressed(animations, Path.Combine(modelDirectory, "Player_Animation_NoFace.sbfres"));
}

static IEnumerable<string> EnumerateTextureFiles(string content, string suffix) =>
    Directory.EnumerateFiles(Path.Combine(content, "Model"), $"Armor_*.{suffix}.sbfres");

static byte[] DecompressMaybe(byte[] data)
{
    if (!data.AsSpan(0, Math.Min(4, data.Length)).SequenceEqual("Yaz0"u8))
        return data;
    int outputSize = checked((int)System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(4, 4)));
    byte[] output = new byte[outputSize];
    int source = 16, destination = 0, bits = 0, code = 0;
    while (destination < output.Length)
    {
        if (bits == 0)
        {
            code = data[source++];
            bits = 8;
        }
        if ((code & 0x80) != 0)
            output[destination++] = data[source++];
        else
        {
            int first = data[source++], second = data[source++];
            int distance = ((first & 0x0f) << 8) | second;
            int length = first >> 4;
            if (length == 0)
                length = data[source++] + 0x12;
            else
                length += 2;
            int copy = destination - distance - 1;
            for (int index = 0; index < length && destination < output.Length; index++)
                output[destination++] = output[copy++];
        }
        code = (code << 1) & 0xff;
        bits--;
    }
    return output;
}

static BfresFile Open(byte[] yaz0Data) => new(new MemoryStream(DecompressMaybe(yaz0Data)));

static void AddLinkModel(BfresFile output, byte[] linkData, string name, Func<string, bool> keepShape)
{
    Model model = Open(linkData).Models[0];
    model.Name = name;
    foreach (string shape in model.Shapes.Keys.Where(shape => !keepShape(shape)).ToArray())
        model.Shapes.RemoveKey(shape);
    output.Models.Add(name, model);
}

static void AddArmorModels(BfresFile output, BfresFile armor)
{
    AddArmorModel(output, armor, "Head");
    AddArmorModel(output, armor, "Upper");
    AddArmorModel(output, armor, "Lower");
}

static void AddDefaultAlias(BfresFile output, byte[] defaultArmor, string part, string alias)
{
    Model? model = Open(defaultArmor).Models.Values.FirstOrDefault(candidate =>
        candidate.Name.Contains(part, StringComparison.Ordinal));
    if (model is null)
        throw new InvalidDataException($"Default armor has no {part} model");
    model.Name = alias;
    output.Models.Add(alias, model);
}

static void AddArmorModel(BfresFile output, BfresFile armor, string part)
{
    Model? model = armor.Models.Values.FirstOrDefault(candidate =>
        candidate.Name.Contains(part, StringComparison.Ordinal));
    if (model is null)
        return;

    string[] pieces = model.Name.Split('_');
    if (pieces.Length < 2)
        throw new InvalidDataException($"Unexpected armor model name: {model.Name}");
    string armorNumber = pieces[1];
    string name = $"MP_{model.Name.Replace("_A", "", StringComparison.Ordinal).Replace("_B", "", StringComparison.Ordinal)}";
    model.Name = name;

    UserData userData = new() { Name = "ArmorNumber" };
    userData.SetValue(new[] { armorNumber });
    if (model.UserData.ContainsKey(userData.Name))
        model.UserData[userData.Name] = userData;
    else
        model.UserData.Add(userData.Name, userData);

    if (output.Models.ContainsKey(name))
        output.Models[name] = model;
    else
        output.Models.Add(name, model);
}

static void ValidateModels(BfresFile output)
{
    Model head = output.Models[$"{ModelName}Head"];
    if (head.Shapes.Count == 0 || head.Shapes.Keys.Any(shape => shape.Contains("Skin", StringComparison.Ordinal)))
        throw new InvalidDataException("Generated Link head has the wrong shape filter");

    Model[] armorModels = output.Models.Values
        .Where(model => model.Name.StartsWith("MP_Armor_", StringComparison.Ordinal))
        .ToArray();
    if (armorModels.Length == 0 || armorModels.Any(model => !model.UserData.ContainsKey("ArmorNumber")))
        throw new InvalidDataException("Generated armor models are missing ArmorNumber metadata");

    foreach (string alias in new[] { $"{ModelName}Helmet", $"{ModelName}Lower", $"{ModelName}Upper" })
        if (!output.Models.ContainsKey(alias) || output.Models[alias].Shapes.Count == 0)
            throw new InvalidDataException($"Generated player model is missing default alias {alias}");
}

static void AddTextures(BfresFile output, BfresFile source)
{
    foreach (TextureShared texture in source.Textures.Values)
    {
        if (output.Textures.ContainsKey(texture.Name))
            output.Textures[texture.Name] = texture;
        else
            output.Textures.Add(texture.Name, texture);
    }
}

static void SaveCompressed(BfresFile file, string path)
{
    using MemoryStream stream = new();
    file.ToBinary(stream);
    File.WriteAllBytes(path, CompressYaz0(stream.ToArray()));
}

// A literal-only Yaz0 stream is intentionally used here. It is completely
// portable, deterministic and avoids CsYaz0's x86-only native library. Cemu
// expands it to the exact same BFRES bytes as a more aggressively compressed
// stream.
static byte[] CompressYaz0(byte[] data)
{
    using MemoryStream output = new(16 + data.Length + (data.Length + 7) / 8);
    output.Write("Yaz0"u8);
    Span<byte> size = stackalloc byte[4];
    System.Buffers.Binary.BinaryPrimitives.WriteUInt32BigEndian(size, checked((uint)data.Length));
    output.Write(size);
    output.Write(new byte[8]);
    for (int offset = 0; offset < data.Length; offset += 8)
    {
        output.WriteByte(0xff);
        output.Write(data, offset, Math.Min(8, data.Length - offset));
    }
    return output.ToArray();
}

static byte[] ReadSarcEntry(byte[] yaz0Data, string wanted)
{
    byte[] data = DecompressMaybe(yaz0Data);
    (int start, int finish) = FindSarcEntryRange(data, wanted);
    return data[start..finish];
}

static (int Start, int End) FindSarcEntryRange(byte[] data, string wanted)
{
    if (!data.AsSpan(0, 4).SequenceEqual("SARC"u8))
        throw new InvalidDataException("TitleBG.pack is not a SARC archive");
    bool bigEndian = data[6] == 0xfe && data[7] == 0xff;
    int headerSize = Read16(data, 4, bigEndian);
    int dataOffset = checked((int)Read32(data, 12, bigEndian));
    if (!data.AsSpan(headerSize, 4).SequenceEqual("SFAT"u8))
        throw new InvalidDataException("TitleBG.pack has no SFAT table");
    int nodeHeaderSize = Read16(data, headerSize + 4, bigEndian);
    int nodeCount = Read16(data, headerSize + 6, bigEndian);
    int nodes = headerSize + nodeHeaderSize;
    int sfnt = nodes + nodeCount * 16;
    int names = sfnt + Read16(data, sfnt + 4, bigEndian);
    for (int index = 0; index < nodeCount; index++)
    {
        int node = nodes + index * 16;
        uint attributes = Read32(data, node + 4, bigEndian);
        int nameOffset = checked((int)(attributes & 0x00ffffff) * 4);
        int end = Array.IndexOf(data, (byte)0, names + nameOffset);
        string name = System.Text.Encoding.UTF8.GetString(data, names + nameOffset, end - names - nameOffset);
        if (name != wanted)
            continue;
        int start = dataOffset + checked((int)Read32(data, node + 8, bigEndian));
        int finish = dataOffset + checked((int)Read32(data, node + 12, bigEndian));
        return (start, finish);
    }
    throw new FileNotFoundException($"{wanted} was not found in TitleBG.pack");
}

static ushort Read16(byte[] data, int offset, bool bigEndian) => bigEndian
    ? System.Buffers.Binary.BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(offset, 2))
    : System.Buffers.Binary.BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(offset, 2));

static uint Read32(byte[] data, int offset, bool bigEndian) => bigEndian
    ? System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(offset, 4))
    : System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(offset, 4));
