#include "dlssnr/NrPreflightState.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace NrPreflightState;
    assert(Signal(false, true, true, true) == State::Inactive);
    assert(Signal(true, false, false, false) == State::Inactive);
    assert(Signal(true, true, false, false) == State::Fault);
    assert(Signal(true, true, true, false) == State::Fault);
    assert(Signal(true, true, true, true) == State::Ready);

    assert(Delivery(false, 4, 0, false) == State::Inactive);
    assert(Delivery(true, 0, 0, false) == State::Inactive);
    assert(Delivery(true, 2, 0, false) == State::Fault);
    assert(Delivery(true, 2, 1, false) == State::Ready);
    assert(Delivery(true, 2, 1, true) == State::Fault);
    std::cout << "PASS: preflight distinguishes inactive, observed, missing, invalid and failed delivery\n";
}
