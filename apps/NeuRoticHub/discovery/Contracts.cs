namespace NeuRotic.Discovery;
internal sealed record DiscoveryRequest(int ProtocolVersion, string Kind, string[]? Roots, string[]? ExcludedRoots = null);
internal sealed record GameCandidate(string Id, string Store, string StoreId, string Title, string InstallRoot, string Availability, string Reason,
    string Executable = "", string[]? ExecutableCandidates = null, string SteamRoot = "", string IconPath = "");
internal sealed record DiscoveryDirectory(string Path, string Source, bool Automatic);
internal sealed record DiscoveryResult(int ProtocolVersion, string Kind, GameCandidate[] Games, string[] Errors,
    [property:System.Text.Json.Serialization.JsonIgnore(Condition=System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)] DiscoveryDirectory[]? Directories = null);
