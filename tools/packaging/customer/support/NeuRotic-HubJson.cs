// Strict duplicate-key/depth validation for the Windows PowerShell 5.1 host.
// Conversion to the typed request remains in the installer owner.
using System;
using System.Collections.Generic;
using System.Text;
public static class NeuRoticHubJson {
 sealed class Reader {
  string text; int pos; bool ignoreCase; int stringLimit;
  public Reader(string value,bool caseInsensitive=true,int maximumString=32768){text=value;ignoreCase=caseInsensitive;stringLimit=maximumString;}
  void Space(){while(pos<text.Length && (text[pos]==' '||text[pos]=='\t'||text[pos]=='\r'||text[pos]=='\n'))pos++;}
  char Next(){if(pos==text.Length)throw new FormatException("Truncated JSON");return text[pos++];}
  string String(){if(Next()!='"')throw new FormatException("Expected JSON string");var b=new StringBuilder();while(true){char c=Next();if(c=='"')return b.ToString();if(c<32)throw new FormatException("Control character");if(c=='\\'){c=Next();switch(c){case '"':case '\\':case '/':break;case 'b':c='\b';break;case 'f':c='\f';break;case 'n':c='\n';break;case 'r':c='\r';break;case 't':c='\t';break;case 'u':int code=0;for(int i=0;i<4;i++){char h=Next();int n=(h>='0'&&h<='9')?h-'0':(h>='a'&&h<='f')?h-'a'+10:(h>='A'&&h<='F')?h-'A'+10:-1;if(n<0)throw new FormatException("Invalid escape");code=code*16+n;}c=(char)code;break;default:throw new FormatException("Invalid escape");}}b.Append(c);if(b.Length>stringLimit)throw new FormatException("String too large");}}
  void Value(int depth){if(depth>16)throw new FormatException("JSON too deeply nested");Space();if(pos==text.Length)throw new FormatException("Missing value");char c=text[pos];if(c=='{'){pos++;var seen=new HashSet<string>(ignoreCase?StringComparer.OrdinalIgnoreCase:StringComparer.Ordinal);Space();if(pos<text.Length&&text[pos]=='}'){pos++;return;}while(true){Space();string key=String();if(!seen.Add(key))throw new FormatException("Duplicate JSON key");Space();if(Next()!=':')throw new FormatException("Expected colon");Value(depth+1);Space();c=Next();if(c=='}')return;if(c!=',')throw new FormatException("Expected comma");}}
   if(c=='['){pos++;Space();if(pos<text.Length&&text[pos]==']'){pos++;return;}int count=0;while(true){if(++count>10000)throw new FormatException("Array too large");Value(depth+1);Space();c=Next();if(c==']')return;if(c!=',')throw new FormatException("Expected comma");}}
   if(c=='"'){String();return;}
   int start=pos;while(pos<text.Length&&!char.IsWhiteSpace(text[pos])&&",]}".IndexOf(text[pos])<0)pos++;string token=text.Substring(start,pos-start);if(token=="true"||token=="false"||token=="null")return;if(!System.Text.RegularExpressions.Regex.IsMatch(token,@"^-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?$"))throw new FormatException("Invalid JSON value");double number;if(!double.TryParse(token,System.Globalization.NumberStyles.Float,System.Globalization.CultureInfo.InvariantCulture,out number)||double.IsNaN(number)||double.IsInfinity(number))throw new FormatException("Nonfinite JSON number");
  }
  public void Validate(){Value(0);Space();if(pos!=text.Length)throw new FormatException("Trailing JSON");}
 }
 public static void ValidateSettingsRequest(string text){new Reader(text,true,4194304).Validate();}
 public static void Validate(string text){new Reader(text).Validate();}
 public static bool IsNonemptyConfigObject(string text){new Reader(text,false,65536).Validate();int i=0;while(i<text.Length&&char.IsWhiteSpace(text[i]))i++;if(i==text.Length||text[i++]!='{')return false;while(i<text.Length&&char.IsWhiteSpace(text[i]))i++;return i<text.Length&&text[i]!='}';}
 public static void ValidateBoolean(object value,string field){if(!(value is bool))throw new FormatException("Request field must be Boolean: "+field);}
 sealed class SteamReader {
  string text;int pos,count;
  public SteamReader(string value){if(value.Length>65536)throw new FormatException("Steam manifest too large");text=value;}
  void Space(){while(pos<text.Length){if(char.IsWhiteSpace(text[pos])){pos++;continue;}if(pos+1<text.Length&&text[pos]=='/'&&text[pos+1]=='/'){while(pos<text.Length&&text[pos]!='\n')pos++;continue;}break;}}
  string Token(){Space();if(pos==text.Length)return null;char c=text[pos++];if(c=='{'||c=='}')return c.ToString();if(c!='"')throw new FormatException("Expected quoted Steam value");var b=new StringBuilder();while(pos<text.Length){c=text[pos++];if(c=='"')return b.ToString();if(c<32)throw new FormatException("Invalid Steam string");if(c=='\\'){if(pos==text.Length)break;c=text[pos++];if(c!='\\'&&c!='"')throw new FormatException("Invalid Steam escape");}b.Append(c);if(b.Length>32768)throw new FormatException("Steam value too large");}throw new FormatException("Truncated Steam string");}
  Dictionary<string,object> Map(int depth,bool nested){if(depth>12)throw new FormatException("Steam manifest too deeply nested");var map=new Dictionary<string,object>(StringComparer.OrdinalIgnoreCase);while(true){string key=Token();if(key==null){if(nested)throw new FormatException("Truncated Steam object");return map;}if(key=="}"){if(!nested)throw new FormatException("Unexpected Steam close");return map;}if(key=="{"||map.ContainsKey(key)||++count>4096)throw new FormatException("Invalid or duplicate Steam key");string value=Token();if(value==null||value=="}")throw new FormatException("Missing Steam value");map.Add(key,value=="{"?(object)Map(depth+1,true):value);}}
  public Dictionary<string,string> Identity(){var root=Map(0,false);object state;if(root.Count!=1||!root.TryGetValue("AppState",out state)||!(state is Dictionary<string,object>))throw new FormatException("Invalid Steam AppState");var app=(Dictionary<string,object>)state;object id,dir;if(!app.TryGetValue("appid",out id)||!(id is string)||!app.TryGetValue("installdir",out dir)||!(dir is string))throw new FormatException("Missing Steam identity");return new Dictionary<string,string>{{"appid",(string)id},{"installdir",(string)dir}};}
 }
 public static Dictionary<string,string> ReadSteamIdentity(string text){return new SteamReader(text).Identity();}
}
