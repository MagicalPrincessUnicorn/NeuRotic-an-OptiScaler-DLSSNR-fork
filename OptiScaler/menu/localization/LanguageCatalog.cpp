#include "LanguageCatalog.h"
#include <stdexcept>
#include <sstream>
#include <regex>
#include <cstdio>
namespace Neurotic::Localization {
ResolvedText Resolve(const CatalogLayers& layers,std::string_view id){if(!layers.english)throw std::runtime_error("Missing canonical catalog.");auto entry=layers.english->find(id);if(entry==layers.english->end())throw std::runtime_error("Unknown canonical string ID.");for(const auto& [pack,layer]:{std::pair{layers.local,Layer::Local},std::pair{layers.community,Layer::Community},std::pair{layers.bundled,Layer::Bundled}}){if(!pack)continue;auto value=pack->entries.find(id);if(value!=pack->entries.end()&&!value->second.text.empty())return {value->second.text,layer,value->second.revision!=entry->second.revision,&entry->second};}return {entry->second.english,Layer::English,false,&entry->second};}
std::string Format(const ResolvedText& resolved,const NamedArguments& arguments){if(!resolved.entry||arguments.size()!=resolved.entry->placeholders.size())throw std::runtime_error("Formatting arguments do not match the template.");std::map<std::string,std::string,std::less<>> values;for(const auto& placeholder:resolved.entry->placeholders){auto arg=arguments.find(placeholder.name);if(arg==arguments.end())throw std::runtime_error("Missing formatting argument: "+placeholder.name);std::string value;if(placeholder.type=="text"){if(!std::holds_alternative<std::string>(arg->second))throw std::runtime_error("Expected text argument.");value=std::get<std::string>(arg->second);}else if(placeholder.type=="integer"){if(!std::holds_alternative<int64_t>(arg->second))throw std::runtime_error("Expected integer argument.");value=std::to_string(std::get<int64_t>(arg->second));}else if(placeholder.type=="number"){if(!std::holds_alternative<double>(arg->second))throw std::runtime_error("Expected numeric argument.");std::ostringstream stream;stream<<std::get<double>(arg->second);value=stream.str();}else throw std::runtime_error("Unsupported placeholder type.");values.emplace(placeholder.name,std::move(value));}
 // Parse only the validated template. Inserted argument bytes are never parsed
 // again as format directives, even if a filename itself contains '%' or '{}'.
 static const std::regex directive(R"(%([-+#0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?)(?:hh|ll|[hljztL]|I64)?([diuoxXfFeEgGaAcsp]))");
 std::string output;size_t printfArgument=0;
 auto append=[&](std::string_view value){if(value.size()>65536-output.size())throw std::runtime_error("Formatted UI text exceeds its limit.");output+=value;};
 auto print=[&](const std::string& format,auto value){int count=std::snprintf(nullptr,0,format.c_str(),value);if(count<0||count>32768)throw std::runtime_error("Formatted value exceeds its limit.");std::string bytes(size_t(count)+1,'\0');std::snprintf(bytes.data(),bytes.size(),format.c_str(),value);bytes.resize(size_t(count));append(bytes);};
 for(size_t pos=0;pos<resolved.text.size();){
  if(resolved.text[pos]=='{'){auto end=resolved.text.find('}',pos+1);if(end!=std::string::npos){auto value=values.find(std::string_view(resolved.text).substr(pos+1,end-pos-1));if(value!=values.end()){append(value->second);pos=end+1;continue;}}}
  if(resolved.text[pos]=='%'&&pos+1<resolved.text.size()&&resolved.text[pos+1]=='%'){append("%");pos+=2;continue;}
  if(resolved.text[pos]=='%'){
   std::cmatch token;auto start=resolved.text.c_str()+pos;
   if(std::regex_search(start,resolved.text.c_str()+resolved.text.size(),token,directive,std::regex_constants::match_continuous)){
    auto flags=token[1].str();for(auto star=flags.find('*');star!=std::string::npos;star=flags.find('*')){auto width=arguments.find("arg"+std::to_string(++printfArgument));if(width==arguments.end()||!std::holds_alternative<int64_t>(width->second))throw std::runtime_error("Expected integer width or precision.");auto value=std::get<int64_t>(width->second);if(value < -32768||value>32768)throw std::runtime_error("Format width or precision exceeds its bound.");if(value<0&&star&&flags[star-1]=='.')flags.erase(star-1,2);else {if(value<0){flags.insert(0,"-");++star;value=-value;}flags.replace(star,1,std::to_string(value));}}
    auto key="arg"+std::to_string(++printfArgument);auto argument=arguments.find(key);if(argument==arguments.end())throw std::runtime_error("Missing formatting argument: "+key);
    const char type=token[2].str()[0];
    if(std::string_view("di").find(type)!=std::string_view::npos){if(!std::holds_alternative<int64_t>(argument->second))throw std::runtime_error("Expected integer format argument.");print("%"+flags+"ll"+type,std::get<int64_t>(argument->second));}
    else if(std::string_view("uoxX").find(type)!=std::string_view::npos){if(!std::holds_alternative<int64_t>(argument->second))throw std::runtime_error("Expected integer format argument.");print("%"+flags+"ll"+type,static_cast<uint64_t>(std::get<int64_t>(argument->second)));}
    else if(std::string_view("fFeEgGaA").find(type)!=std::string_view::npos){if(!std::holds_alternative<double>(argument->second))throw std::runtime_error("Expected number format argument.");print("%"+flags+type,std::get<double>(argument->second));}
    else {if(!std::holds_alternative<std::string>(argument->second))throw std::runtime_error("Expected text format argument.");const auto& text=std::get<std::string>(argument->second);if(text.find('\0')!=std::string::npos||text.size()>32768)throw std::runtime_error("Invalid text format argument.");if(type=='s')print("%"+flags+"s",text.c_str());else append(text);}
    pos+=token.length();continue;
   }
  }
  append(std::string_view(resolved.text).substr(pos++,1));
 }
 return output;
}
#include "EnglishInventory.inc"
const EnglishCatalog& CanonicalEnglish(){static const EnglishCatalog catalog=[](){std::string json;for(const auto* chunk:EmbeddedEnglishChunks)json+=chunk;EnglishCatalog value;for(const auto& item:Json::parse(json)){EnglishEntry entry;entry.id=item.at("id");entry.surface=item.at("surface");entry.section=item.at("section");entry.control=item.at("control");entry.english=item.at("english");entry.context=item.at("context");entry.lineBreaks=item.at("line_breaks");entry.revision=item.at("revision");entry.preserve=item.at("preserve").get<std::vector<std::string>>();for(const auto& p:item.at("placeholders"))entry.placeholders.push_back({p.at("name"),p.at("type")});value.emplace(entry.id,std::move(entry));}return value;}();return catalog;}
Json EnglishTemplate(){Json result={{"formatVersion",1},{"packId","english-template"},{"locale",""},{"language",""},{"name","NeuRotic English Template"},{"author","NeuRotic"},{"version","1"},{"desktop",Json::array()},{"in_game",Json::array()}};for(const auto& [id,e]:CanonicalEnglish()){Json placeholders=Json::array();for(const auto& p:e.placeholders)placeholders.push_back({{"name",p.name},{"type",p.type}});result[e.surface].push_back({{"id",id},{"translation",""},{"revision",e.revision},{"surface",e.surface},{"section",e.section},{"control",e.control},{"english",e.english},{"context",e.context},{"placeholders",placeholders},{"preserve",e.preserve},{"line_breaks",e.lineBreaks}});}return result;}
}
