// SPDX-License-Identifier: GPL-3.0-only
#include "avs_capture.h"
#include "iidx_protocol.h"
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
    auto *n=node(value); ++n->tree->reads; assert(requested==11 && n->type==11);
    if(n->raise_error) n->tree->error=92;
    if(n->throw_value) throw std::runtime_error("synthetic observer failure");
    if(n->fail_value) return -1;
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
DecodeOutcome decode(Tree &tree) { return decode_property(&tree,functions()); }
void expect(Tree &tree,DecodeKind kind) { assert(decode(tree).kind==kind); }
DecodeDiagnostic expect_fault(Tree &tree,DecodePoint point,DecodeIssue issue) {
    const auto result=decode(tree);
    assert(result.kind==DecodeKind::Fault);
    assert(result.diagnostic.point==point && result.diagnostic.issue==issue);
    return result.diagnostic;
}
void expect_event(Tree &tree,Observation::Kind kind,std::string_view id={}) {
    const auto result=decode(tree);
    assert(result.kind==DecodeKind::Event && result.event.kind==kind);
    assert(result.event.generation==0 && result.event.success);
    assert(std::string_view(result.event.profile_id.data())==id);
    if(!id.empty()) {
        std::array<char,32> expected{}; assert(copy_profile_id(id,expected));
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
    { Tree tree; auto *service=tree.profile(); service->attributes->next=nullptr; expect(tree,DecodeKind::Fault); }
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
    { Tree tree; auto *service=tree.profile(); service->attributes->value="1"; service->attributes->next->type=11;
      expect(tree,DecodeKind::Fault); }
}
void ignored_properties() {
    assert(decode_property(nullptr,functions()).kind==DecodeKind::Ignore);
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
    // Root lookup failures that introduce an AVS error remain capture faults.
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
    { Tree tree; tree.get(); auto f=functions(); f.refer=nullptr; assert(decode_property(&tree,f).kind==DecodeKind::Fault); }
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
} // namespace
int main() {
    successful_events(); malformed_fields(); ignored_properties(); rootless_properties();
    errors_and_termination(); traversal_boundaries();
    std::puts("bounded AVS decoder protocol boundaries passed");
}
