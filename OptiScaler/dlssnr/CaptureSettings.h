#pragma once
namespace DlssNr::CaptureSettings {
// Display normalization only. Existing saved values are not rewritten on read.
constexpr int TransferChoice(int transport,bool fallback) noexcept {
 return transport==0?(fallback?0:1):transport==1||transport==2?transport:-1;
}
struct Transfer {int transport;bool fallback;};
constexpr Transfer TransferPolicy(int choice) noexcept {return {choice,choice==0};}
}
