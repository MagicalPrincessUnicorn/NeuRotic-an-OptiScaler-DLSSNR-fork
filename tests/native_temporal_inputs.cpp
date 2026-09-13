#include <dlssnr/NativeTemporalInputs.h>
#include <cassert>
#include <cstdio>

int main()
{
    for (unsigned bits = 0; bits != 16; ++bits)
        assert(DlssNr::NativeTemporalInputs::Authoritative(bits & 1, bits & 2, bits & 4, bits & 8) == (bits == 7));
    std::puts("PASS native backend authority: SR/DLSS/direct-DX12 only");
}
