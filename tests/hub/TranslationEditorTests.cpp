#include "languages/TranslationEditorModel.h"
#include <cstdio>
using namespace nh;
int main(){int fails=0;auto check=[&](bool ok,const char* text){printf("%s %s\n",ok?"PASS":"FAIL",text);fails+=!ok;};
 const auto& entry=Neurotic::Localization::CanonicalEnglish().begin()->second;
 Json doc={{"formatVersion",1},{"packId","editor-fixture"},{"locale","pl"},{"language","Polski"},{"name","Polski"},{"author",""},{"version","1"},{"desktop",Json::array()},{"in_game",Json::array()}};
 TranslationEditorModel editor;editor.Load(doc);check(!editor.Dirty(),"opening creates independent clean draft");
 editor.Select(entry.id);editor.SetTranslation("Tłumaczenie");check(editor.Dirty(),"edit marks draft dirty");
 editor.SetFilter(EditorFilter::Complete);auto list=editor.Rows();check(!list.empty()||!editor.Validation(entry.id).empty(),"complete filter includes only usable edits");
 editor.SetSearch("no-such-id-unique");check(editor.Rows().empty(),"search filters virtualized list");editor.SetSearch("");
 check(editor.RequestClose()==CloseState::Ask,"dirty close requires a choice");editor.CancelClose();check(editor.CloseStatus()==CloseState::Open&&editor.Dirty(),"Cancel preserves draft and editor");
 editor.MarkSaved();check(!editor.Dirty()&&editor.RequestClose()==CloseState::Ready,"saved draft can close");
 editor.Load(doc);editor.Select(entry.id);editor.SetTranslation("bad %n");check(!editor.Validation(entry.id).empty(),"invalid format is reported per entry");
 check(editor.Document()[entry.surface].size()==1,"invalid draft remains editable and serializable");
 editor.DiscardClose();check(editor.CloseStatus()==CloseState::Ready,"Discard explicitly closes dirty work");
 return fails?1:0;
}
