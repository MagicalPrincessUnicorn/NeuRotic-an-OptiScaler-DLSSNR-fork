#include "ui/HubViewModel.h"
#include <iostream>
#include <cstring>
#define NH_NUMERIC_CORE_ONLY
#if __has_include("ui/NumericSettings.h")
#include "ui/NumericSettings.h"
#define NH_NUMERIC_AVAILABLE
#endif
int RunNumericSettingsTests(){
 int failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failed+=!ok;};
#ifdef NH_NUMERIC_AVAILABLE
 using namespace nh;
 SettingsField field;field.available=true;field.type="float";field.hasRange=true;field.minimum=0;field.maximum=2;field.section="DlssNr";field.key="Intensity";
 for(auto text:{"auto","AUTO","0","2","0.913245678901","1e-5"})check(ParseNumericDraft(text).valid,"numeric or automatic configuration parses without normalization");
 for(auto text:{"nan","inf","1abc","","--1"})check(!ParseNumericDraft(text).valid,"nonfinite or malformed draft remains invalid");
 auto automatic=ParseNumericDraft("auto");check(automatic.automatic,"auto stays an explicit automatic draft");
 std::vector<SettingsField> fields{field};auto range=NumericSliderRange(field,fields);check(range.enabled&&!range.integer&&range.minimum==0&&range.maximum==2,"known float range comes from existing schema");
 field.original="0.913245678901";strcpy_s(field.draft.data(),field.draft.size(),field.original.c_str());auto before=field.draft;
 check(ParseNumericDraft(field.draft.data()).valid&&field.draft==before,"reading a precise saved value does not change its text");
 check(SetNumericDraft(field,fields,.913245678901)&&ParseNumericDraft(field.draft.data()).value==.913245678901,"slider draft round-trips the same double value");
 before=field.draft;check(!SetNumericDraft(field,fields,2.1)&&field.draft==before,"range rejection leaves draft untouched");
 field.type="keycode";check(!NumericSliderRange(field,fields).enabled,"key codes are never sliders");field.type="float";field.hasRange=false;check(!NumericSliderRange(field,fields).enabled,"unbounded numeric fields remain manual");
 field.hasRange=true;field.type="integer";field.key="Passes";field.minimum=1;field.maximum=10;fields={field};check(SetNumericDraft(field,fields,8)&&std::string(field.draft.data())=="8","integer sliders retain whole-number serialization");before=field.draft;check(!SetNumericDraft(field,fields,8.5)&&field.draft==before,"integer slider cannot silently round a fractional request");
 SettingsField mode;mode.section="DlssNr";mode.key="EnhancedResolution";strcpy_s(mode.draft.data(),mode.draft.size(),"3");field.key="EnhancedCustomScale";field.minimum=0;field.maximum=200;fields={field,mode};range=NumericSliderRange(field,fields);check(range.enabled&&range.minimum==25&&range.maximum==200,"manual scale respects existing coupled 25-200 percent bounds");strcpy_s(fields[1].draft.data(),fields[1].draft.size(),"2");check(!NumericSliderRange(field,fields).enabled,"legacy scale preset codes are not percent sliders");
#else
 check(false,"bounded numeric settings have a reusable slider draft adapter");
#endif
 return failed;
}
#ifdef NH_NUMERIC_TEST_MAIN
int main(){return RunNumericSettingsTests()?1:0;}
#endif
