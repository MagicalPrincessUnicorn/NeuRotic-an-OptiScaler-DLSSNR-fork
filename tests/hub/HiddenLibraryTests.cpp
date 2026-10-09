#include "ui/HubViewModel.h"
#include <fstream>
#include <iostream>

// The focused entry point requires an explicit disposable AppData replacement.
// No fixture executable is valid, so selection cannot inspect an installed game.
int RunHiddenLibraryTests() {
    wchar_t fixtureRoot[32768]{};
    if (!GetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT", fixtureRoot, 32768)) {
        std::cerr << "FAIL HIDDEN: explicit fixture root is required\n";
        return 1;
    }
    int failed=0;
    auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<"HIDDEN: "<<label<<'\n';if(!ok)++failed;};
    auto cache=nh::UserRoot()/L"library.json";
    if(std::filesystem::exists(cache))throw std::runtime_error("Hidden-library fixture must start without a library cache");
    std::filesystem::create_directories(nh::UserRoot());
    auto gameRoot=nh::Utf8((nh::UserRoot()/L"Game A").wstring());
    auto otherRoot=nh::Utf8((nh::UserRoot()/L"Game B").wstring());
    auto wait=[](nh::HubModel& model){
        // Load starts a read-only package preflight. Drain it without Poll(),
        // which can connect the unrelated Anything worker after success.
        nh::Json result;auto until=GetTickCount64()+15000;
        while(model.readiness.busy&&GetTickCount64()<until){model.readiness.Poll(result);Sleep(1);}
        if(model.readiness.busy)throw std::runtime_error("Fixture preflight did not finish");
    };
    try {
        nh::HubModel model;
        model.games={{"stable-a","A game","Steam",gameRoot},{"stable-b","B game","GOG",otherRoot}};
        model.games[0].favorite=true;
        model.Select(0);
        model.ToggleHidden(0);
        check(model.games[0].hidden&&model.selected==0&&model.OrderedGames().size()==2,"hide keeps selected row and detail until selection changes");
        model.Select(0);
        check(model.OrderedGames().size()==2,"reselecting the same game does not dismiss it");
        model.Select(1);
        check(model.OrderedGames()==std::vector<int>{1},"another selection removes the hidden game from All");
        model.SetLibraryFilter(nh::LibraryFilter::FavoritesFirst);
        check(model.OrderedGames()==std::vector<int>{1},"Favorites first excludes hidden favorites");
        model.SetLibraryFilter(nh::LibraryFilter::Hidden);
        check(model.selected==-1&&model.OrderedGames()==std::vector<int>{0},"Hidden contains only hidden games and clears excluded details");
        model.Select(0);model.ToggleHidden(0);
        check(!model.games[0].hidden&&model.OrderedGames()==std::vector<int>{0},"show keeps selected row in Hidden until leaving");
        model.LeaveLibrarySelection();
        check(model.selected==-1&&model.OrderedGames().empty(),"page departure removes the temporary shown row and details");
        model.SetLibraryFilter(nh::LibraryFilter::All);model.Select(0);model.ToggleHidden(0);
        model.SetLibraryFilter(nh::LibraryFilter::FavoritesFirst);
        check(model.selected==-1&&model.OrderedGames()==std::vector<int>{1},"filter change dismisses the hidden selection");
        model.SetLibraryFilter(nh::LibraryFilter::Hidden);model.Select(0);model.ToggleHidden(0);model.ToggleHidden(0);
        check(model.games[0].hidden&&model.OrderedGames()==std::vector<int>{0},"repeated toggles retain coherent filter membership");
        model.SetLibraryFilter(nh::LibraryFilter::All);model.Select(1);model.ToggleHidden(1);
        std::swap(model.games[0],model.games[1]);
        check(model.OrderedGames().empty(),"row indices cannot transfer a visibility exemption to a different game");
        std::swap(model.games[0],model.games[1]);model.LeaveLibrarySelection();
        model.games[1].hidden=false;model.Save();
        nh::HubModel restored;restored.Load();wait(restored);
        check(restored.games.size()==2&&restored.games[0].hidden&&!restored.games[1].hidden&&restored.games[0].favorite,"fresh model round-trips hidden state and favorites through library.json");
        auto discovery=nh::Json{{"protocolVersion",2},{"kind","DiscoveryResult"},{"errors",nh::Json::array()},{"games",nh::Json::array({{{"id","new-discovery-id"},{"title","Rediscovered A"},{"store","Steam"},{"installRoot",gameRoot},{"hidden",false}}})}};
        restored.MergeDiscovery(discovery);
        check(restored.games.size()==2&&restored.games[0].id=="stable-a"&&restored.games[0].hidden&&restored.games[0].favorite,"rescan preserves stable identity and local hidden state over discovery metadata");
        restored.Save();nh::HubModel afterRescan;afterRescan.Load();wait(afterRescan);
        check(afterRescan.games.size()==2&&afterRescan.games[0].hidden&&afterRescan.OrderedGames()==std::vector<int>{1},"rescan state survives another AppData round trip");
        nh::Json legacy;{std::ifstream input(cache);legacy=nh::Json::parse(input);}
        for(auto& game:legacy["games"])game.erase("hidden");
        {std::ofstream output(cache);output<<legacy.dump();}
        nh::HubModel upgraded;upgraded.Load();wait(upgraded);
        check(upgraded.games.size()==2&&!upgraded.games[0].hidden&&!upgraded.games[1].hidden&&upgraded.OrderedGames().size()==2,"existing caches without hidden state remain visible after update");
        std::filesystem::remove(cache);
    } catch(const std::exception& error) {
        std::cerr<<"FAIL HIDDEN: "<<error.what()<<'\n';return 1;
    }
    return failed;
}
