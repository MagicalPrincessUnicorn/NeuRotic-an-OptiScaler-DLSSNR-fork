#include "external/FidelityFX-CheckedClosure/CheckedControlledService.h"
_Static_assert(sizeof(FfxNrOwnedOutputHandleV1)==16,"handle ABI");
_Static_assert(sizeof(FfxNrAlgorithmHandleV1)==16,"algorithm ABI");
_Static_assert(sizeof(FfxNrControlledContractV1)==32,"contract ABI");
int controlled_c_abi(void){FfxNrControlledServiceV1 api={0};api.size=sizeof(api);return api.version;}
