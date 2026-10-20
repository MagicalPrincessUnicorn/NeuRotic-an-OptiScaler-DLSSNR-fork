#include "ui/FileConflictReview.h"
#include <cassert>
#include <iostream>
int main(){
 nh::FileConflictReview review;
 const nh::Json files=nh::Json::array({{{"path","dxgi.dll"}},{{"path","OptiScaler/feature.dll"}}});
 review.Begin(files);
 assert(review.Current().at("path")=="dxgi.dll");
 assert(!review.Choose("Replace"));
 assert(review.Current().at("path")=="OptiScaler/feature.dll");
 assert(review.Choose("Replace"));
 assert(review.Decisions().size()==2&&review.Decisions()[0].size()==2&&!review.Decisions()[0].contains("sha256"));
 review.Begin(files);assert(!review.Choose("Skip"));assert(review.Decisions()[0]["action"]=="Skip");
 review.Cancel();assert(review.Decisions().empty()&&!review.Active());
 for(const auto* action:{"Bogus","SaveAndReplace"}){review.Begin(files);bool rejected=false;try{review.Choose(action);}catch(...){rejected=true;}assert(rejected&&review.Decisions().empty());}
 std::cout<<"PASS per-file order, three choices, no SHA-bound decisions, cancel clears choices, invalid action refused\n";
}
