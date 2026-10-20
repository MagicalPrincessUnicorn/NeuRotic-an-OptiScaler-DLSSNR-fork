#include <iostream>
#if __has_include(<nr/lifecycle/Fsr3SelectedInputObservation.h>)
#define NR_SOURCE_TRANSACTION_TESTING
#include <nr/lifecycle/Fsr3SelectedInputObservation.h>
namespace L=Neurotic::Lifecycle;
namespace C=Neurotic::Contracts;
namespace Neurotic::Lifecycle {
struct SourceTransactionTestAccess {
    static auto Create(NativeSourceTransactionObservation::ReadFactory factory)
    {
        auto value=std::shared_ptr<NativeSourceTransactionObservation>(new NativeSourceTransactionObservation({},{}));
        value->SelectedOutput({},1,1,std::move(factory));value->Returned(true);return value;
    }
};
}
struct Pin final:L::NativeSourceTransactionObservation::SelectedOutputRead
{
    bool& current;int& alive;ID3D12Resource* native;C::ResourceView view;
    Pin(bool& c,int& a,ID3D12Resource* n):current(c),alive(a),native(n){++alive;}
    ~Pin(){--alive;}
    bool Current()const noexcept override{return current;}
    ID3D12Resource* Native()const noexcept override{return native;}
    const C::ResourceView& View()const noexcept override{return view;}
};
int main()
{
    int checks=0,failures=0;auto check=[&](bool ok,const char* n){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<n<<'\n';}};
    bool current=true;int alive=0,calls=0,marker=0;
    auto* resource=reinterpret_cast<ID3D12Resource*>(&marker);
    auto source=L::SourceTransactionTestAccess::Create([&](ID3D12Resource*){++calls;return std::make_unique<Pin>(current,alive,resource);});
    L::SourceAssociationObservation linked{L::SourceAssociationReason::MissingConsumerRights,source};
    {
        L::Fsr3SelectedInputObservation pin(linked,resource);
        check(pin.Current()&&alive==1,"exact selected write pinned across callback");
        check(pin.Status()==L::Fsr3SelectedInputStatus::CurrentSelectedWrite,"current source observation distinguished");
        current=false;
        check(!pin.Current()&&pin.Status()==L::Fsr3SelectedInputStatus::Revoked,"write invalidation revokes observation during dispatch");
        check(alive==1,"revocation does not drop pin before callback returns");
    }
    check(alive==0,"callback scope returns Resource pin");current=true;
    int other=0;
    L::Fsr3SelectedInputObservation mismatch(linked,reinterpret_cast<ID3D12Resource*>(&other));
    check(!mismatch.Current()&&alive==0,"different presentColor cannot inherit selected write by scheduling key");
    for(auto reason:{L::SourceAssociationReason::Pending,L::SourceAssociationReason::Ambiguous,L::SourceAssociationReason::AlreadyObserved,L::SourceAssociationReason::Failed})
    {
        auto before=calls;L::Fsr3SelectedInputObservation refused({reason,source},resource);
        check(!refused.Current()&&calls==before,"ineligible association cannot reacquire now-successful source");
    }
    std::cout<<"FSR selected input: "<<checks<<" checks, "<<failures<<" failures; CPU only\n";return failures?1:0;
}
#else
int main(){std::cerr<<"FAIL retained FSR selected input observation missing\n";return 1;}
#endif
