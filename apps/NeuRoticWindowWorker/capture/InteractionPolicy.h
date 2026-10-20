#pragma once
#include <windows.h>
namespace nrw {
inline const char* InteractionBlockReason(bool overlay,bool sourceEnabled,bool ownedDialog,bool guiKnown,DWORD guiFlags){
 if(!overlay)return nullptr;
 if(!sourceEnabled)return "source-disabled";
 if(ownedDialog)return "source-dialog-active";
 if(!guiKnown)return "source-gui-unavailable";
 if(guiFlags&(GUI_INMENUMODE|GUI_POPUPMENUMODE|GUI_SYSTEMMENUMODE|GUI_INMOVESIZE))return "source-menu-or-move";
 return nullptr;
}
}
