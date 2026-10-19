// SPDX-License-Identifier: GPL-3.0-only
#include "avs_capture.h"
#include "iidx_protocol.h"
#include "sdvx_protocol.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace cabinet;
namespace {
struct Tree;
struct TestNode {
    Tree *tree{};
    std::string name;
    std::string value;
    uint8_t byte{};
    int type=1;
    TestNode *children{};
    TestNode *attributes{};
    TestNode *next{};
    bool bad_name{},bad_value{},fail_name{},fail_value{},raise_error{},throw_value{},untouched{};
};
struct Tree {
    std::vector<std::unique_ptr<TestNode>> nodes;
    TestNode *root{};
    uint32_t error{};
    unsigned searches{},clears{},reads{},refers{};
    bool search_error{};
    int successful_return{}; // AVS success is >=0, including zero, not a string length.
    explicit Tree(std::string root_name="call") { root=make(std::move(root_name)); }
    TestNode *make(std::string name,int type=1,std::string value={}) {
        auto node=std::make_unique<TestNode>();
        node->tree=this; node->name=std::move(name); node->type=type; node->value=std::move(value);
        auto *result=node.get(); nodes.push_back(std::move(node)); return result;
    }
    static void append(TestNode *&head,TestNode *node) {
        if(!head) { head=node; return; }
        auto *last=head; while(last->next) last=last->next; last->next=node;
    }
    TestNode *child(TestNode *owner,std::string name,int type=1,std::string value={}) {
        auto *node=make(std::move(name),type,std::move(value)); append(owner->children,node); return node;
    }
    TestNode *attr(TestNode *owner,std::string name,std::string value) {
        auto *node=make(std::move(name),46,std::move(value)); append(owner->attributes,node); return node;
    }
    TestNode *request(std::string method) {
        auto *service=child(root,"IIDX33pc"); attr(service,"method",std::move(method)); return service;
    }
    TestNode *get() {
        auto *service=request("get"); attr(service,"cid","synthetic-card"); attr(service,"rid","synthetic-ref"); return service;
    }
    TestNode *profile(std::string id="10000001") {
        root->name="response";
        auto *service=child(root,"IIDX33pc"); attr(service,"status","0"); attr(service,"fault","0");
        auto *pcdata=child(service,"pcdata"); attr(pcdata,"id",std::move(id)); return service;
    }
    TestNode *sdvx_request(std::string method,std::string refid="a1B2c3D4e5F60718") {
        auto *service=child(root,"game"); attr(service,"method",std::move(method));
        child(service,"refid",11,std::move(refid)); return service;
    }
    TestNode *sdvx_profile(std::string code="20000001",uint8_t result=0) {
        root->name="response";
        auto *service=child(root,"game"); child(service,"code",11,std::move(code));
        child(service,"result",3)->byte=result; return service;
    }
};
TestNode *node(Node value) { assert(value); auto *n=static_cast<TestNode *>(value); assert(!n->untouched); return n; }
int copy_string(std::string_view value,void *out,uint32_t capacity,bool nonterminated,int result) {
    if(nonterminated) std::memset(out,'X',capacity);
    else {
        const size_t length=std::min(value.size()+1,static_cast<size_t>(capacity));
        std::memcpy(out,value.data(),std::min(value.size(),length));
        if(length>value.size()) static_cast<char *>(out)[value.size()]=0;
    }
    return result;
}
int32_t destroy(Property) { assert(false && "decoder must not destroy the property"); return 0; }
Node search(Property property,Node parent,const char *path) {
    auto &tree=*static_cast<Tree *>(property); ++tree.searches;
    assert(!parent && std::strcmp(path,"/")==0);
    if(tree.search_error) tree.error=91;
    return tree.root;
}
Node traverse(Node value,int direction) {
    auto *n=node(value);
    if(direction==1) return n->children;
    if(direction==2) return n->attributes;
    assert(direction==4); return n->next;
}
int32_t name(Node value,char *out,uint32_t capacity) {
    auto *n=node(value);
    if(n->fail_name) return -1;
    return copy_string(n->name,out,capacity,n->bad_name,n->tree->successful_return);
}
int type(Node value) { return node(value)->type; }
int read(Node value,int requested,void *out,uint32_t capacity) {
    auto *n=node(value); ++n->tree->reads;
    assert(requested==n->type && (requested==11 || requested==3));
    if(n->raise_error) n->tree->error=92;
    if(n->throw_value) throw std::runtime_error("synthetic observer failure");
    if(n->fail_value) return -1;
    if(requested==3) {
        assert(capacity==sizeof(uint8_t));
        *static_cast<uint8_t *>(out)=n->byte; return n->tree->successful_return;
    }
    return copy_string(n->value,out,capacity,n->bad_value,n->tree->successful_return);
}
int refer(Property property,Node owner,const char *path,int requested,void *out,uint32_t capacity) {
    auto &tree=*static_cast<Tree *>(property); ++tree.refers;
    const std::string_view key(path); assert(!key.empty() && key.back()=='@' && requested==46);
    for(auto *n=node(owner)->attributes;n;n=n->next) {
        if(n->name!=key.substr(0,key.size()-1)) continue;
        assert(!n->untouched && n->type==46);
        if(n->raise_error) tree.error=93;
        if(n->throw_value) throw std::runtime_error("synthetic observer failure");
        if(n->fail_value) return -1;
        return copy_string(n->value,out,capacity,n->bad_value,tree.successful_return);
    }
    assert(false && "refer must only address an existing attribute"); return -1;
}
uint32_t get_error(Property property) { return static_cast<Tree *>(property)->error; }
Property clear_error(Property property) { auto &tree=*static_cast<Tree *>(property); ++tree.clears; tree.error=0; return property; }
AvsFunctions functions() {
    AvsFunctions f;
    f.destroy=destroy; f.search=search; f.traverse=traverse; f.name=name; f.node_type=type;
    f.read=read; f.refer=refer; f.get_error=get_error; f.clear_error=clear_error;
    return f;
}
DecodeOutcome decode(Tree &tree,GameKind game=GameKind::Iidx) { return decode_property(&tree,functions(),game); }
void expect(Tree &tree,DecodeKind kind,GameKind game=GameKind::Iidx) { assert(decode(tree,game).kind==kind); }
DecodeDiagnostic expect_fault(Tree &tree,DecodePoint point,DecodeIssue issue) {
    const auto result=decode(tree);
    assert(result.kind==DecodeKind::Fault);
    assert(result.diagnostic.point==point && result.diagnostic.issue==issue);
    return result.diagnostic;
}
void expect_event(Tree &tree,Observation::Kind kind,std::string_view id={},GameKind game=GameKind::Iidx) {
    const auto result=decode(tree,game);
    assert(result.kind==DecodeKind::Event && result.event.kind==kind);
    assert(result.event.generation==0 && result.event.success);
    assert(std::string_view(result.event.profile_id.data())==id);
    if(!id.empty()) {
        std::array<char,32> expected{}; assert(id.size()<expected.size());
        std::copy(id.begin(),id.end(),expected.begin());
        assert(result.event.profile_id==expected); // Consistent bytes for state-machine identity equality.
    }
}
void successful_events() {
    { Tree tree; tree.get(); expect_event(tree,Observation::Kind::GetRequested); }
    { Tree tree; tree.profile(); expect_event(tree,Observation::Kind::ProfileLoaded,"10000001"); }
    { Tree tree; tree.successful_return=9; tree.profile("10000002"); expect_event(tree,Observation::Kind::ProfileLoaded,"10000002"); }
    { Tree tree; auto *service=tree.request("logout"); tree.child(service,"iidx_id",11,"10000001");
      expect_event(tree,Observation::Kind::LogoutRequested,"10000001"); assert(tree.reads==1); }
    { Tree tree; auto *service=tree.request("save"); tree.attr(service,"iidxid","10000001");
      expect_event(tree,Observation::Kind::SaveRequested,"10000001"); assert(tree.reads==0); }
    { Tree tree; tree.profile(std::string(31,'1')); expect_event(tree,Observation::Kind::ProfileLoaded,std::string(31,'1')); }
}
void malformed_fields() {
    for(const auto *id:{"","12X45678","-1000001","1000 001","12345678901234567890123456789012"}) {
        Tree tree; tree.profile(id); expect(tree,DecodeKind::Fault);
    }
    { Tree tree; auto *service=tree.profile(); service->children->attributes=nullptr; expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.profile(); service->attributes->value=""; expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.profile(); service->children->attributes->type=11;
      const auto diagnostic=expect_fault(tree,DecodePoint::ProfileId,DecodeIssue::WrongType);
      assert(diagnostic.observed_type==11 && diagnostic.network_root); }
    { Tree tree; auto *service=tree.profile(); tree.attr(service->children,"id","10000002"); expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.profile(); tree.child(service,"pcdata"); expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.request("get"); tree.attr(service,"rid","ref");
      expect_fault(tree,DecodePoint::GetCid,DecodeIssue::Missing); }
    { Tree tree; auto *service=tree.request("get"); tree.attr(service,"cid","card"); expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.get(); service->attributes->next->value=""; expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.get(); service->attributes->next->type=11; expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.get(); tree.attr(service,"cid","duplicate"); expect(tree,DecodeKind::Fault); }
    { Tree tree; tree.child(tree.root,"IIDX33pc"); expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.request("logout"); tree.attr(service,"iidx_id","10000001"); expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.request("logout"); tree.child(service,"iidx_id",1,"10000001"); expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.request("save"); tree.child(service,"iidxid",11,"10000001"); expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.request("save"); tree.attr(service,"iidxid","1000x001"); expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.request("logout"); tree.child(service,"iidx_id",11,""); expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.get(); service->attributes->next->next->value=""; expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.get(); tree.attr(service,"method","get"); expect(tree,DecodeKind::Fault); }
    { Tree tree; tree.get(); tree.get(); expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.profile(); service->attributes=service->attributes->next; expect(tree,DecodeKind::Fault); }
}
void iidx_fault_attributes() {
    { Tree tree; auto *service=tree.profile(); service->attributes->next=nullptr;
      expect_event(tree,Observation::Kind::ProfileLoaded,"10000001"); }
    { Tree tree; auto *service=tree.profile(); service->attributes->value="1"; service->attributes->next=nullptr;
      expect(tree,DecodeKind::Ignore); }
    { Tree tree; auto *service=tree.profile(); service->attributes->next->value="";
      expect_fault(tree,DecodePoint::ResponseFault,DecodeIssue::Empty); }
    { Tree tree; auto *service=tree.profile(); service->attributes->next->type=11;
      expect_fault(tree,DecodePoint::ResponseFault,DecodeIssue::WrongType); }
    { Tree tree; auto *service=tree.profile(); tree.attr(service,"fault","0");
      expect_fault(tree,DecodePoint::ResponseFault,DecodeIssue::Duplicate); }
}
void ignored_properties() {
    assert(decode_property(nullptr,functions(),GameKind::Iidx).kind==DecodeKind::Ignore);
    { Tree tree("unrelated"); auto *bad=tree.child(tree.root,"never-inspected"); bad->untouched=true; expect(tree,DecodeKind::Ignore); }
    { Tree tree; auto *service=tree.child(tree.root,"cardmng"); tree.attr(service,"method","inquire")->untouched=true; expect(tree,DecodeKind::Ignore); }
    { Tree tree; tree.request("common"); expect(tree,DecodeKind::Ignore); }
    { Tree tree("response"); auto *service=tree.child(tree.root,"IIDX33pc"); tree.attr(service,"status","0");
      expect(tree,DecodeKind::Ignore); assert(tree.refers==0); }
    { Tree tree; auto *service=tree.profile(); service->attributes->value="1"; service->children->attributes=nullptr;
      expect(tree,DecodeKind::Ignore); }
    { Tree tree; auto *service=tree.profile(); service->attributes->next->value="1"; service->children->attributes=nullptr;
      expect(tree,DecodeKind::Ignore); }
    { Tree tree; auto *service=tree.profile(); auto *bin=tree.child(service->children,"bin",999,std::string(100000,'x'));
      bin->untouched=true; auto *broken=tree.child(bin,std::string(1000,'?')); broken->untouched=true;
      auto *other=tree.child(service,"music",999); auto *illegal=tree.child(other,"2pp"); illegal->untouched=true;
      expect_event(tree,Observation::Kind::ProfileLoaded,"10000001"); }
}
void rootless_properties() {
    // A clean rootless object must not poison the cabinet state before login.
    { Tree tree; tree.root=nullptr;
      expect(tree,DecodeKind::Ignore);
      assert(tree.error==0 && tree.clears==0); }
    // Root lookup failures that introduce an AVS error remain decoder faults.
    { Tree tree; tree.root=nullptr; tree.search_error=true;
      const auto diagnostic=expect_fault(tree,DecodePoint::RootSearch,DecodeIssue::Missing);
      assert(diagnostic.avs_error==91 && !diagnostic.network_root);
      assert(tree.error==0 && tree.clears==1); }
}
void errors_and_termination() {
    { Tree tree; tree.get(); tree.error=77; expect(tree,DecodeKind::Ignore);
      assert(tree.error==77 && tree.searches==0 && tree.clears==0); }
    { Tree tree; auto *service=tree.get(); service->attributes->next->raise_error=true;
      const auto diagnostic=expect_fault(tree,DecodePoint::GetRid,DecodeIssue::AvsError);
      assert(diagnostic.avs_error==93 && tree.error==0 && tree.clears==1); }
    { Tree tree("irrelevant"); tree.search_error=true; expect(tree,DecodeKind::Ignore); assert(tree.error==0 && tree.clears==1); }
    { Tree tree; auto *service=tree.get(); service->attributes->fail_value=true; expect(tree,DecodeKind::Fault); assert(tree.clears==0); }
    { Tree tree; auto *service=tree.get(); service->attributes->throw_value=true; service->attributes->raise_error=true;
      const auto diagnostic=expect_fault(tree,DecodePoint::RequestMethod,DecodeIssue::ObserverException);
      assert(diagnostic.avs_error==93 && tree.error==0 && tree.clears==1); }
    { Tree tree; auto *service=tree.request("logout"); tree.child(service,"iidx_id",11,"10000001")->raise_error=true;
      expect(tree,DecodeKind::Fault); assert(tree.error==0 && tree.clears==1); }
    { Tree tree; tree.get(); tree.root->bad_name=true;
      const auto diagnostic=expect_fault(tree,DecodePoint::RootName,DecodeIssue::Unterminated);
      assert(!diagnostic.network_root); }
    { Tree tree; auto *service=tree.get(); service->fail_name=true; expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.get(); service->attributes->bad_name=true; expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.get(); service->attributes->bad_value=true; expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.profile(); service->children->attributes->bad_value=true; expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.request("logout"); tree.child(service,"iidx_id",11,"10000001")->bad_value=true;
      expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.request("logout"); tree.child(service,"iidx_id",11,"10000001")->fail_value=true;
      const auto diagnostic=expect_fault(tree,DecodePoint::LogoutId,DecodeIssue::AccessorFailed);
      assert(diagnostic.accessor_result==-1); }
    { Tree tree; auto *service=tree.get(); tree.attr(service,std::string(64,'x'),"ignored"); expect(tree,DecodeKind::Fault); }
    { Tree tree; tree.get(); auto f=functions(); f.refer=nullptr; assert(decode_property(&tree,f,GameKind::Iidx).kind==DecodeKind::Fault); }
}
void traversal_boundaries() {
    // Exactly 256 nodes per chain and exactly 512 total are permitted.
    { Tree tree; auto *service=tree.get();
      for(int i=0;i<255;++i) tree.child(tree.root,"other");
      for(int i=0;i<253;++i) tree.attr(service,"other","ignored");
      expect_event(tree,Observation::Kind::GetRequested); }
    { Tree tree; tree.get(); for(int i=0;i<256;++i) tree.child(tree.root,"other");
      expect_fault(tree,DecodePoint::Services,DecodeIssue::TraversalLimit); }
    { Tree tree; auto *service=tree.get(); for(int i=0;i<254;++i) tree.attr(service,"other","ignored"); expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.profile();
      for(int i=0;i<254;++i) tree.child(tree.root,"other");
      for(int i=0;i<254;++i) tree.attr(service,"other","ignored");
      // root children 255 + service children 1 + service attrs 256 = 512;
      // the profile id would be node 513, so no partial event may escape.
      expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.get(); service->next=service; expect(tree,DecodeKind::Fault); }
    { Tree tree; auto *service=tree.get(); service->attributes->next->next->next=service->attributes;
      expect(tree,DecodeKind::Fault); }
}
void sdvx_events_and_isolation() {
    constexpr auto game=GameKind::Sdvx;
    { Tree tree; tree.sdvx_request("sv7_load");
      expect_event(tree,Observation::Kind::GetRequested,"a1B2c3D4e5F60718",game); }
    { Tree tree; tree.sdvx_request("sv7_save","ABCDEF0123456789");
      expect_event(tree,Observation::Kind::SaveRequested,"ABCDEF0123456789",game); }
    { Tree tree; tree.sdvx_profile(); expect_event(tree,Observation::Kind::ProfileLoaded,{},game); }
    { Tree tree; tree.successful_return=9; auto *service=tree.sdvx_profile();
      auto *payload=tree.child(service,"music",999); tree.child(payload,"uninspected")->untouched=true;
      expect_event(tree,Observation::Kind::ProfileLoaded,{},game); }
    for(const auto *method:{"sv7_save_m","sv7_save_e","sv7_play_s","sv7_play_e","sv7_frozen","common"}) {
        Tree tree; auto *service=tree.sdvx_request(method,"invalid");
        service->children->untouched=true; expect(tree,DecodeKind::Ignore,game);
    }
    for(const auto *service_name:{"cardmng","eacoin","IIDX33pc"}) {
        Tree tree; auto *service=tree.child(tree.root,service_name);
        tree.attr(service,"method","sv7_load")->untouched=true; expect(tree,DecodeKind::Ignore,game);
    }
    { Tree tree; tree.get(); expect(tree,DecodeKind::Ignore,game); }
    { Tree tree; tree.profile(); expect(tree,DecodeKind::Ignore,game); }
    { Tree tree; tree.sdvx_request("sv7_load"); expect(tree,DecodeKind::Ignore,GameKind::Iidx); }
    { Tree tree; tree.sdvx_profile(); expect(tree,DecodeKind::Ignore,GameKind::Iidx); }
    { Tree tree; tree.sdvx_request("sv7_load"); expect(tree,DecodeKind::Ignore,GameKind::Unknown); }
    // A hexadecimal request identity must not weaken IIDX's numeric profile validation.
    { Tree tree; tree.profile("a1B2c3D4e5F60718"); expect(tree,DecodeKind::Fault,GameKind::Iidx); }
}
void sdvx_malformed_fields() {
    constexpr auto game=GameKind::Sdvx;
    for(const auto *method:{"sv7_load","sv7_save"}) {
        for(const auto *id:{"","123456789012345","12345678901234567","a1B2c3D4e5F6071G"}) {
            Tree tree; tree.sdvx_request(method,id); expect(tree,DecodeKind::Fault,game);
        }
        { Tree tree; auto *service=tree.sdvx_request(method); service->children=nullptr;
          expect(tree,DecodeKind::Fault,game); }
        { Tree tree; auto *service=tree.sdvx_request(method); service->children->type=46;
          expect(tree,DecodeKind::Fault,game); }
        { Tree tree; auto *service=tree.sdvx_request(method);
          tree.child(service,"refid",11,"a1B2c3D4e5F60718"); expect(tree,DecodeKind::Fault,game); }
        { Tree tree; auto *service=tree.sdvx_request(method); service->children->bad_value=true;
          expect(tree,DecodeKind::Fault,game); }
    }
    for(const auto *code:{"","2000A001","-2000001","2000 001","12345678901234567890123456789012"}) {
        Tree tree; tree.sdvx_profile(code); expect(tree,DecodeKind::Fault,game);
    }
    { Tree tree; auto *service=tree.sdvx_profile(); service->children->type=3;
      expect(tree,DecodeKind::Fault,game); }
    { Tree tree; auto *service=tree.sdvx_profile(); tree.child(service,"code",11,"20000002");
      expect(tree,DecodeKind::Fault,game); }
    { Tree tree; auto *service=tree.sdvx_profile(); service->children->next=nullptr;
      expect(tree,DecodeKind::Fault,game); }
    for(int wrong_type:{1,11,46}) {
        Tree tree; auto *service=tree.sdvx_profile(); service->children->next->type=wrong_type;
        expect(tree,DecodeKind::Fault,game);
    }
    { Tree tree; auto *service=tree.sdvx_profile(); tree.child(service,"result",3);
      expect(tree,DecodeKind::Fault,game); }
    { Tree tree; auto *service=tree.sdvx_profile(); service->children->next->fail_value=true;
      expect(tree,DecodeKind::Fault,game); }
    { Tree tree; auto *service=tree.sdvx_profile(); service->children->next->raise_error=true;
      expect(tree,DecodeKind::Fault,game); assert(tree.error==0 && tree.clears==1); }
    { Tree tree; auto *service=tree.sdvx_profile(); service->children->next->throw_value=true;
      expect(tree,DecodeKind::Fault,game); }
}
DecodeKind observe(StatusState &state,Tree &tree,GameKind game=GameKind::Sdvx) {
    const auto generation=state.generation();
    auto result=decode(tree,game);
    if(result.kind==DecodeKind::Event) {
        result.event.generation=generation; state.apply(result.event);
    } else if(result.kind==DecodeKind::Fault) {
        const auto kind=result.diagnostic.point==DecodePoint::Accessors
            ? Observation::Kind::CaptureFault : Observation::Kind::ProtocolFault;
        state.apply({kind,generation,{},false});
    }
    return result.kind;
}
void sdvx_state_trajectory() {
    using K=Observation::Kind;
    StatusState state;
    const auto resource=[&](K kind) { state.apply({kind,state.generation(),{},true}); };
    resource(K::IdleArchiveOpened); resource(K::IdleArchiveMounted);
    assert(state.snapshot()==Availability::Free);
    { Tree tree; tree.sdvx_request("sv7_load"); assert(observe(state,tree)==DecodeKind::Event); }
    assert(state.snapshot()==Availability::Unavailable);
    // Uncorrelated save/play replies are common while a load is pending.
    { Tree tree("response"); tree.child(tree.root,"game");
      assert(observe(state,tree)==DecodeKind::Ignore); }
    { Tree tree("response"); auto *service=tree.child(tree.root,"game"); tree.child(service,"result",3);
      assert(observe(state,tree)==DecodeKind::Ignore); }
    { Tree tree("response"); auto *service=tree.child(tree.root,"game"); tree.child(service,"music",1);
      assert(observe(state,tree)==DecodeKind::Ignore); }
    assert(state.snapshot()==Availability::Unavailable);
    { Tree tree; tree.sdvx_profile("20000001"); assert(observe(state,tree)==DecodeKind::Event); }
    assert(state.snapshot()==Availability::Occupied && state.summary().phase==Phase::Occupied);
    for(const auto *method:{"sv7_save_m","sv7_save_e"}) {
        Tree tree; tree.sdvx_request(method); assert(observe(state,tree)==DecodeKind::Ignore);
        assert(state.summary().phase==Phase::Occupied);
    }
    { Tree tree; tree.sdvx_request("sv7_save","ABCDEF0123456789"); observe(state,tree); }
    assert(state.summary().phase==Phase::Occupied);
    // Save correlation must use the request refid, not the unrelated numeric response code.
    { Tree tree; tree.sdvx_request("sv7_save"); assert(observe(state,tree)==DecodeKind::Event); }
    assert(state.summary().phase==Phase::Ending && state.snapshot()==Availability::Occupied);
    resource(K::IdleArchiveOpened); assert(state.snapshot()==Availability::Occupied);
    resource(K::IdleArchiveMounted); assert(state.snapshot()==Availability::Free);
    StatusState failed;
    { Tree tree; tree.sdvx_request("sv7_load"); observe(failed,tree); }
    { Tree tree; tree.sdvx_profile("20000001",1); assert(observe(failed,tree)==DecodeKind::Ignore); }
    assert(failed.snapshot()==Availability::Unavailable);
    failed.apply({K::IdleArchiveOpened,failed.generation(),{},true});
    failed.apply({K::IdleArchiveMounted,failed.generation(),{},true});
    assert(failed.snapshot()==Availability::Free);
    { Tree tree; tree.sdvx_request("sv7_load"); observe(failed,tree); }
    { Tree tree; tree.sdvx_profile(); observe(failed,tree); }
    assert(failed.snapshot()==Availability::Occupied);
    StatusState unexpected;
    { Tree tree; tree.sdvx_profile(); observe(unexpected,tree); }
    assert(unexpected.snapshot()==Availability::Unavailable);
}
void iidx_guest_trajectory_and_isolation() {
    using K=Observation::Kind;
    constexpr auto game=GameKind::Iidx;
    StatusState state;
    const auto resource=[&](K kind,bool success=true) { state.apply({kind,state.generation(),{},success}); };
    const auto idle=[&] { resource(K::IdleArchiveOpened); resource(K::IdleArchiveMounted); };
    const auto play=[&] {
        Tree tree; auto *service=tree.request("playstart");
        // Guest play has no card identifiers, and unrelated payloads are not read.
        tree.child(service,"unrelated",999)->untouched=true;
        assert(observe(state,tree,game)==DecodeKind::Event);
        assert(state.snapshot()==Availability::Occupied);
    };
    idle(); assert(state.snapshot()==Availability::Free);
    // Card inquiry and PIN cancellation do not establish a session.
    { Tree tree; auto *service=tree.child(tree.root,"cardmng"); tree.attr(service,"method","inquire");
      assert(observe(state,tree,game)==DecodeKind::Ignore); }
    idle(); assert(state.snapshot()==Availability::Free);
    for(const auto *service_name:{"cardmng","eacoin","game","IIDX32pc"}) {
        Tree tree; auto *service=tree.child(tree.root,service_name);
        tree.attr(service,"method","playstart")->untouched=true;
        assert(observe(state,tree,game)==DecodeKind::Ignore);
        assert(state.snapshot()==Availability::Free);
    }
    for(auto other_game:{GameKind::Sdvx,GameKind::Unknown}) {
        Tree tree; tree.request("playstart");
        assert(observe(state,tree,other_game)==DecodeKind::Ignore);
        assert(state.snapshot()==Availability::Free);
    }
    { Tree tree; tree.sdvx_request("playstart");
      assert(observe(state,tree,GameKind::Sdvx)==DecodeKind::Ignore);
      assert(state.snapshot()==Availability::Free); }
    resource(K::IdleArchiveOpened); play();
    assert(state.summary().reason==Reason::PlayStarted);
    resource(K::IdleArchiveMounted); assert(state.snapshot()==Availability::Occupied);
    play();
    resource(K::IdleArchiveOpened,false); resource(K::IdleArchiveMounted);
    assert(state.snapshot()==Availability::Occupied);
    resource(K::IdleArchiveOpened); resource(K::IdleArchiveMounted,false);
    assert(state.snapshot()==Availability::Occupied);
    idle(); assert(state.snapshot()==Availability::Free);
    // Guest play during login must not lose the request needed by the response.
    { Tree tree; tree.get(); assert(observe(state,tree,game)==DecodeKind::Event); }
    assert(state.snapshot()==Availability::Unavailable); play();
    { Tree tree; tree.profile(); assert(observe(state,tree,game)==DecodeKind::Event); }
    assert(state.snapshot()==Availability::Occupied);
    { Tree tree; auto *service=tree.request("save"); tree.attr(service,"iidxid","10000001");
      assert(observe(state,tree,game)==DecodeKind::Event); }
    assert(state.snapshot()==Availability::Occupied && state.summary().phase==Phase::Ending);
    idle(); assert(state.snapshot()==Availability::Free);
    play(); idle(); assert(state.snapshot()==Availability::Free);
    // Malformed observations remain unavailable until a fresh idle boundary.
    { Tree tree; auto *service=tree.request("playstart"); tree.attr(service,"method","playstart");
      assert(observe(state,tree,game)==DecodeKind::Fault); }
    assert(state.snapshot()==Availability::Unavailable);
    { Tree tree; tree.request("playstart"); assert(observe(state,tree,game)==DecodeKind::Event); }
    assert(state.snapshot()==Availability::Unavailable);
    idle(); assert(state.snapshot()==Availability::Free);
    play(); assert(state.snapshot()==Availability::Occupied);
}
void iidx_playstart_method_boundaries() {
    { Tree tree; auto *service=tree.request("playstart"); service->attributes->type=11;
      expect_fault(tree,DecodePoint::RequestMethod,DecodeIssue::WrongType); }
    { Tree tree; auto *service=tree.request("playstart"); service->attributes->bad_value=true;
      expect_fault(tree,DecodePoint::RequestMethod,DecodeIssue::Unterminated); }
    { Tree tree; auto *service=tree.request("playstart"); service->attributes->fail_value=true;
      expect_fault(tree,DecodePoint::RequestMethod,DecodeIssue::AccessorFailed); }
    { Tree tree; auto *service=tree.request("playstart"); service->attributes->raise_error=true;
      expect_fault(tree,DecodePoint::RequestMethod,DecodeIssue::AvsError);
      assert(tree.error==0 && tree.clears==1); }
    { Tree tree; tree.request(""); expect_fault(tree,DecodePoint::RequestMethod,DecodeIssue::Empty); }
    for(const auto *method:{"playStart","playstart_extra"}) {
        Tree tree; tree.request(method); expect(tree,DecodeKind::Ignore);
    }
}
void iidx_two_profile_trajectory() {
    using K=Observation::Kind;
    constexpr auto game=GameKind::Iidx;
    constexpr auto first="10000002",second="10000001";
    StatusState state;
    const auto idle=[&] {
        state.apply({K::IdleArchiveOpened,state.generation(),{},true});
        state.apply({K::IdleArchiveMounted,state.generation(),{},true});
    };
    idle();
    for(int i=0;i<2;++i) { Tree tree; tree.get(); assert(observe(state,tree,game)==DecodeKind::Event); }
    { Tree tree; tree.profile(first); assert(observe(state,tree,game)==DecodeKind::Event); }
    assert(state.snapshot()==Availability::Occupied);
    { Tree tree; tree.request("playstart"); assert(observe(state,tree,game)==DecodeKind::Event); }
    { Tree tree; tree.profile(second); assert(observe(state,tree,game)==DecodeKind::Event); }
    assert(state.snapshot()==Availability::Occupied);
    { Tree tree; auto *service=tree.request("save"); tree.attr(service,"iidxid",first);
      assert(observe(state,tree,game)==DecodeKind::Event); }
    assert(state.snapshot()==Availability::Occupied && state.summary().phase==Phase::Occupied);
    { Tree tree; auto *service=tree.request("logout"); tree.child(service,"iidx_id",11,second);
      assert(observe(state,tree,game)==DecodeKind::Event); }
    assert(state.snapshot()==Availability::Occupied && state.summary().phase==Phase::Ending);
    idle(); assert(state.snapshot()==Availability::Free);
}
void iidx_response_recovery() {
    using K=Observation::Kind;
    constexpr auto game=GameKind::Iidx;
    for(std::string_view response:{"missing_fault","status_error","fault_error","malformed_fault","bare_error"}) {
        StatusState state;
        const auto idle=[&] {
            state.apply({K::IdleArchiveOpened,state.generation(),{},true});
            state.apply({K::IdleArchiveMounted,state.generation(),{},true});
        };
        idle();
        { Tree tree; tree.get(); observe(state,tree,game); }
        Tree tree;
        auto *service=tree.profile();
        if(response=="missing_fault" || response=="status_error") service->attributes->next=nullptr;
        if(response=="status_error" || response=="bare_error") service->attributes->value="1";
        if(response=="fault_error") service->attributes->next->value="1";
        if(response=="malformed_fault") service->attributes->next->value="";
        if(response=="bare_error") service->children=nullptr;
        const auto kind=observe(state,tree,game);
        if(response=="missing_fault") {
            assert(kind==DecodeKind::Event && state.snapshot()==Availability::Occupied);
            Tree save; auto *request=save.request("save"); save.attr(request,"iidxid","10000001");
            observe(state,save,game);
            assert(state.summary().phase==Phase::Ending && state.snapshot()==Availability::Occupied);
        } else {
            assert(kind==(response=="malformed_fault"?DecodeKind::Fault:DecodeKind::Ignore));
            assert(state.snapshot()==Availability::Unavailable);
        }
        idle(); assert(state.snapshot()==Availability::Free);
        { Tree next; next.get(); observe(state,next,game); }
        { Tree next; auto *reply=next.profile("10000002"); reply->attributes->next=nullptr;
          observe(state,next,game); }
        assert(state.snapshot()==Availability::Occupied);
    }
}
} // namespace
int main() {
    successful_events(); malformed_fields(); ignored_properties(); rootless_properties();
    errors_and_termination(); traversal_boundaries();
    sdvx_events_and_isolation(); sdvx_malformed_fields(); sdvx_state_trajectory();
    iidx_guest_trajectory_and_isolation(); iidx_playstart_method_boundaries(); iidx_two_profile_trajectory();
    iidx_fault_attributes(); iidx_response_recovery();
    std::puts("bounded AVS decoder protocol boundaries passed");
}
