#pragma once
#include <storage/NrDllProfiles.h>
#include <future>

namespace nh {
// File work and hashing run outside the render thread. Poll publishes complete
// snapshots only; the caller includes Busy() in its closing-session drain.
class NrDllManager {
public:
 explicit NrDllManager(std::filesystem::path root,bool writable=false):root_(std::move(root)),writable_(writable){}
 const NrDllCatalog& Catalog() const noexcept{return catalog_;}
 const std::filesystem::path& Root() const noexcept{return root_;}
 bool Ready() const noexcept{return ready_;}
 bool CatalogCurrent() const noexcept{return catalogCurrent_;}
 bool Writable() const noexcept{return writable_;}
 bool Busy() const noexcept{return worker_.valid();}
 std::optional<NrDllErrorCode> Error() const noexcept{return error_;}
 unsigned long SystemError() const noexcept{return systemError_;}
 bool Refresh();bool Import(std::filesystem::path source,std::string name);
 bool Rename(std::string id,std::string name);bool Remove(std::string id);
 bool SetDefault(std::string id);bool ClearDefault();
 void Poll();void RequestStop() noexcept;void CancelCurrent() noexcept{stop_.request_stop();}
private:
 enum class Action {Refresh,Import,Rename,Remove,SetDefault,ClearDefault};
 struct Result {std::optional<NrDllCatalog> catalog;std::optional<NrDllErrorCode> error;unsigned long systemError=0;};
 bool Start(Action action,std::filesystem::path source={},std::string id={},std::string name={});
 std::filesystem::path root_;bool writable_=false,stopping_=false,ready_=false,catalogCurrent_=false;
 NrDllCatalog catalog_;std::optional<NrDllErrorCode> error_;unsigned long systemError_=0;
 std::future<Result> worker_;std::stop_source stop_;
};
}
