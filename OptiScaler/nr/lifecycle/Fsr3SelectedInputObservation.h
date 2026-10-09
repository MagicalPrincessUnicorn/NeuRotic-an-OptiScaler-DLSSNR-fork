#pragma once
#include "NativeSourceTransactionObservation.h"

namespace Neurotic::Lifecycle
{
enum class Fsr3SelectedInputStatus { Unavailable,CurrentSelectedWrite,Revoked };
// Stack-scoped observation at the actual consumer call. The existing Resource
// owner retains the selected write; a scheduling key or native pointer alone
// cannot create this pin. No C03 right, source-bound authorization, ancestry
// across copies, or SDK/provider release is issued here.
class Fsr3SelectedInputObservation
{
    std::unique_ptr<NativeSourceTransactionObservation::SelectedOutputRead> read_;
  public:
    Fsr3SelectedInputObservation(const SourceAssociationObservation& source,ID3D12Resource* exact)
    {
        if(!exact||!source.source||source.reason!=SourceAssociationReason::MissingConsumerRights)return;
        const auto selected=source.source->SelectedOutput();
        if(!selected)return;
        auto read=source.source->AcquireSelectedOutputRead(exact);
        if(read&&read->Native()==exact&&read->View()==selected->view&&read->Current())read_=std::move(read);
    }
    bool Current()const noexcept{return read_&&read_->Current();}
    Fsr3SelectedInputStatus Status()const noexcept
    {return !read_?Fsr3SelectedInputStatus::Unavailable:Current()?Fsr3SelectedInputStatus::CurrentSelectedWrite:Fsr3SelectedInputStatus::Revoked;}
};
inline const char* Fsr3SelectedInputStatusName(Fsr3SelectedInputStatus status)noexcept
{
    switch(status)
    {
    case Fsr3SelectedInputStatus::CurrentSelectedWrite:return "EXACT_SELECTED_WRITE_CURRENT";
    case Fsr3SelectedInputStatus::Revoked:return "SELECTED_WRITE_REVOKED";
    default:return "SELECTED_WRITE_OR_COPY_LINEAGE_UNAVAILABLE";
    }
}
}
