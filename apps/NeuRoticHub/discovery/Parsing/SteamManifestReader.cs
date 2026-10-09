namespace NeuRotic.Discovery;
// Steam KeyValues1 reader, independent implementation of the read-only manifest
// strategy in DLSS Swapper SteamLibrary.cs at ab9b1e2d4bb04187c48fe58eeea06c2b0ea71fbb.
// No Game.ProcessGame, database, DLL processing, image downloads or driver API.
internal static class SteamManifestReader
{
    public static Dictionary<string, object> Read(string text)
    {
        if (text.Length > 1024 * 1024) throw new FormatException("Manifest exceeds 1 MiB");
        int pos = 0;
        string? Token()
        {
            while (pos < text.Length)
            {
                if (char.IsWhiteSpace(text[pos])) { pos++; continue; }
                if (text[pos] == '/' && pos + 1 < text.Length && text[pos + 1] == '/') { while (pos < text.Length && text[pos] != '\n') pos++; continue; }
                break;
            }
            if (pos == text.Length) return null;
            var c = text[pos++];
            if (c is '{' or '}') return c.ToString();
            if (c != '"') throw new FormatException("Expected quoted KeyValues token");
            var value = new System.Text.StringBuilder();
            while (pos < text.Length)
            {
                c = text[pos++];
                if (c == '"') return value.ToString();
                if (c == '\\') { if (pos == text.Length) break; c = text[pos++]; if (c is not ('\\' or '"')) throw new FormatException("Unsupported escape"); }
                value.Append(c);
            }
            throw new FormatException("Truncated quoted token");
        }
        Dictionary<string, object> Object(int depth, bool closing)
        {
            if (depth > 16) throw new FormatException("Manifest too deeply nested");
            var result = new Dictionary<string, object>(StringComparer.OrdinalIgnoreCase);
            while (true)
            {
                var key = Token();
                if (key is null) { if (closing) throw new FormatException("Truncated object"); return result; }
                if (key == "}") { if (!closing) throw new FormatException("Unexpected close"); return result; }
                if (key == "{") throw new FormatException("Missing key");
                var value = Token() ?? throw new FormatException("Missing value");
                if (!result.TryAdd(key, value == "{" ? Object(depth + 1, true) : value)) throw new FormatException("Duplicate manifest key");
                if (value == "}") throw new FormatException("Missing value");
            }
        }
        return Object(0, false);
    }
    public static Dictionary<string, object> Section(Dictionary<string, object> doc, string key) => doc.TryGetValue(key, out var value) && value is Dictionary<string, object> section ? section : throw new FormatException($"Missing {key}");
    public static string Value(Dictionary<string, object> doc, string key) => doc.TryGetValue(key, out var value) && value is string text ? text : throw new FormatException($"Missing {key}");
}
