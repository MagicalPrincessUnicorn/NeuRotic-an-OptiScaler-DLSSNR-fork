#include <menu/Localization.h>
#pragma once
#include "../../../OptiScaler/menu/localization/LanguageRuntime.h"
namespace nh {
inline std::string ResultMessage(const Neurotic::Localization::Json& object,std::string fallback){using namespace Neurotic::Localization;try{if(object.contains("reasonCode")&&object["reasonCode"].is_string()){auto code=object["reasonCode"].get<std::string>();if(CanonicalEnglish().contains(code)){NamedArguments arguments;if(object.contains("messageParameters")&&object["messageParameters"].is_object())for(auto& item:object["messageParameters"].items()){if(item.value().is_string())arguments[item.key()]=item.value().get<std::string>();else if(item.value().is_number_integer())arguments[item.key()]=item.value().get<int64_t>();else if(item.value().is_number())arguments[item.key()]=item.value().get<double>();}return Format(SharedText(code),arguments);}}return object.value(Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason"),fallback);}catch(...){return fallback;}}
inline std::string ResultDetail(const Neurotic::Localization::Json& value){
 if(value.is_string())return value.get<std::string>();
 if(!value.is_object())return {};
 const auto reason=ResultMessage(value,value.value("reason",std::string{}));
 const auto path=value.value("path",std::string{});
 return path.empty()?reason:path+": "+reason;
}
}
