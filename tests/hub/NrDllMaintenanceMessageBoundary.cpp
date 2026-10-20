#include <menu/Localization.h>
// Message boundary only: actual maintenance file/identity/plan/deletion code is
// retained. This fixture does not exercise catalog translation or UI rendering.
namespace Neurotic {
const char* UiLiteral(std::string_view,const char* english){return english;}
std::string UiMessage(std::string_view,const char* english){return english;}
}
