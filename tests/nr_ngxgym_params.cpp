#include <nvsdk_ngx.h>
// NVIDIA's inline optimal-settings helper uses the C parameter API. OptiScaler
// returns the ordinary C++ parameter interface; adapt only these host-side calls.
NVSDK_NGX_API void NVSDK_CONV NVSDK_NGX_Parameter_SetUI(NVSDK_NGX_Parameter* p, const char* name, unsigned int value)
{ p->Set(name, value); }
NVSDK_NGX_API void NVSDK_CONV NVSDK_NGX_Parameter_SetI(NVSDK_NGX_Parameter* p, const char* name, int value)
{ p->Set(name, value); }
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetUI(NVSDK_NGX_Parameter* p, const char* name, unsigned int* value)
{ return p->Get(name, value); }
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetF(NVSDK_NGX_Parameter* p, const char* name, float* value)
{ return p->Get(name, value); }
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetVoidPointer(NVSDK_NGX_Parameter* p, const char* name, void** value)
{ return p->Get(name, value); }
