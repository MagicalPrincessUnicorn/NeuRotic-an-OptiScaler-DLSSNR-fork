using System.Text;
using System.Text.Json;
namespace NeuRotic.Discovery;
internal static class Program
{
    static readonly JsonSerializerOptions Json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, PropertyNameCaseInsensitive = false };
    public static async Task<int> Main(string[] args)
    {
        try
        {
            if (args is ["--self-test", var root]) return await DiscoveryTests.Run(root);
            using var cancel = new CancellationTokenSource(TimeSpan.FromSeconds(40));
            var input = new byte[1048577]; int length = 0;
            var stream = Console.OpenStandardInput();
            while (length < input.Length) { int n = await stream.ReadAsync(input.AsMemory(length), cancel.Token); if (n == 0) break; length += n; }
            if (length > 1048576) throw new ArgumentException("Request exceeds 1 MiB");
            var text = new UTF8Encoding(false, true).GetString(input, 0, length);
            using var document = JsonDocument.Parse(text, new JsonDocumentOptions { MaxDepth = 16 });
            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (var field in document.RootElement.EnumerateObject()) if (!seen.Add(field.Name) || field.Name is not ("protocolVersion" or "kind" or "roots" or "excludedRoots")) throw new ArgumentException("Duplicate or unknown field");
            var request = JsonSerializer.Deserialize<DiscoveryRequest>(text, Json) ?? throw new ArgumentException("Missing request");
            if (!((request.ProtocolVersion == 1 && request.Kind == "DiscoverSteam") || (request.ProtocolVersion == 2 && request.Kind is "DiscoverGames" or "DiscoverFolder"))) throw new ArgumentException("Unsupported request");
            if(seen.Contains("excludedRoots") && request.ProtocolVersion!=2)throw new ArgumentException("Unknown field for request kind");
            var result = request.ProtocolVersion==1 ? await SteamProvider.Discover(request.Roots, cancel.Token)
                : request.Kind=="DiscoverFolder" ? await DiscoveryCoordinator.DiscoverFolder(request.Roots,cancel.Token,request.ExcludedRoots)
                : await DiscoveryCoordinator.Discover(request.Roots,cancel.Token,excludedRoots:request.ExcludedRoots);
            Console.WriteLine(JsonSerializer.Serialize(result, Json)); return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e.Message); return 1; }
    }
}
