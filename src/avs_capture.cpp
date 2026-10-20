// SPDX-License-Identifier: GPL-3.0-only
// Read-only AVS ABI usage: spice2x 26-09-12, avs/core.h and avs/automap.cpp.
#include "avs_capture.h"
#include "iidx_protocol.h"
#include "sdvx_protocol.h"
#include <array>
#include <cstring>
#include <string_view>

namespace cabinet {
namespace {
constexpr int u8_type=3;
constexpr int string_type=11;
constexpr int attribute_type=46;
constexpr int first_child=1;
constexpr int first_attribute=2;
constexpr int next_sibling=4;
constexpr size_t chain_limit=256;
constexpr size_t total_limit=512;

struct Field {
    Node node{};
    bool duplicate{};
    void accept(Node value) noexcept {
        if(node) duplicate=true;
        else node=value;
    }
};

class Decoder {
public:
    Decoder(Property property,const AvsFunctions &functions,GameKind game): property_(property), f_(functions), game_(game) {}

    DecodeOutcome decode() {
        point(DecodePoint::RootSearch);
        Node root=f_.search(property_,nullptr,"/");
        if(!root) return fault(DecodeIssue::Missing);
        std::array<char,64> root_name;
        point(DecodePoint::RootName);
        if(!name(root,root_name)) return fault();
        const bool request=std::strcmp(root_name.data(),"call")==0;
        if(!request && std::strcmp(root_name.data(),"response")!=0) return {};
        diagnostic_.network_root=true;
        point(DecodePoint::Services);

        Field service;
        if(!scan(root,first_child,[&](Node node,std::string_view key) {
            if(game_==GameKind::Sdvx ? key=="game" : (key=="IIDX33pc" || key=="IIDX34pc")) service.accept(node);
        })) return fault();
        if(!service.node) return {};
        if(service.duplicate) return fault(DecodeIssue::Duplicate);
        target_=true;
        if(game_==GameKind::Sdvx)
            return request ? sdvx_request(service.node) : sdvx_response(service.node);
        return request ? request_event(service.node) : response_event(service.node);
    }

    bool target() const noexcept { return target_; }
    DecodeDiagnostic diagnostic() const noexcept { return diagnostic_; }

private:
    void point(DecodePoint point) noexcept {
        diagnostic_.point=point;
        diagnostic_.issue=DecodeIssue::None;
        diagnostic_.observed_type=-1;
        diagnostic_.accessor_result=0;
    }
    bool failure(DecodeIssue issue) noexcept { diagnostic_.issue=issue; return false; }
    DecodeOutcome fault(DecodeIssue issue=DecodeIssue::None) noexcept {
        if(issue!=DecodeIssue::None) diagnostic_.issue=issue;
        return {DecodeKind::Fault,{},diagnostic_};
    }

    bool name(Node node,std::array<char,64> &out) {
        out.fill('\x7f');
        diagnostic_.accessor_result=f_.name(node,out.data(),static_cast<uint32_t>(out.size()));
        if(diagnostic_.accessor_result<0) return failure(DecodeIssue::AccessorFailed);
        if(!std::memchr(out.data(),0,out.size())) return failure(DecodeIssue::Unterminated);
        return true;
    }

    template<class Visitor>
    bool scan(Node owner,int direction,Visitor visit) {
        size_t chain=0;
        for(Node node=f_.traverse(owner,direction); node; node=f_.traverse(node,next_sibling)) {
            if(++chain>chain_limit || ++total_>total_limit) return failure(DecodeIssue::TraversalLimit);
            std::array<char,64> key;
            if(!name(node,key)) return false;
            visit(node,std::string_view(key.data()));
        }
        return true;
    }

    bool value(Node owner,const Field &field,const char *attribute_path,
               std::array<char,32> &out,DecodePoint where) {
        point(where);
        if(!field.node) return failure(DecodeIssue::Missing);
        if(field.duplicate) return failure(DecodeIssue::Duplicate);
        const int type=attribute_path ? attribute_type : string_type;
        diagnostic_.observed_type=f_.node_type(field.node);
        if(diagnostic_.observed_type!=type) return failure(DecodeIssue::WrongType);
        out.fill('\x7f');
        const int result=attribute_path
            ? f_.refer(property_,owner,attribute_path,type,out.data(),static_cast<uint32_t>(out.size()))
            : f_.read(field.node,type,out.data(),static_cast<uint32_t>(out.size()));
        diagnostic_.accessor_result=result;
        if(result<0) return failure(DecodeIssue::AccessorFailed);
        if(out[0]==0) return failure(DecodeIssue::Empty);
        const auto *end=static_cast<const char *>(std::memchr(out.data(),0,out.size()));
        if(!end) return failure(DecodeIssue::Unterminated);
        for(size_t i=static_cast<size_t>(end-out.data()); i<out.size(); ++i) out[i]=0;
        return true;
    }

    DecodeOutcome event(Observation::Kind kind,const std::array<char,32> &id={}) {
        return {DecodeKind::Event,{kind,0,id,true}};
    }

    DecodeOutcome request_event(Node service) {
        Field method,cid,rid,iidxid;
        point(DecodePoint::RequestAttributes);
        if(!scan(service,first_attribute,[&](Node node,std::string_view key) {
            if(key=="method") method.accept(node);
            else if(key=="cid") cid.accept(node);
            else if(key=="rid") rid.accept(node);
            else if(key=="iidxid") iidxid.accept(node);
        })) return fault();
        std::array<char,32> method_value;
        if(!value(service,method,"method@",method_value,DecodePoint::RequestMethod)) return fault();
        const std::string_view method_name(method_value.data());
        if(method_name=="playstart") return event(Observation::Kind::PlayStarted);
        if(method_name=="get") {
            std::array<char,32> scratch;
            if(!value(service,cid,"cid@",scratch,DecodePoint::GetCid) ||
               !value(service,rid,"rid@",scratch,DecodePoint::GetRid)) return fault();
            return event(Observation::Kind::GetRequested);
        }
        if(method_name=="save") {
            std::array<char,32> id;
            if(!value(service,iidxid,"iidxid@",id,DecodePoint::SaveId)) return fault();
            if(!valid_profile_id(id)) return fault(DecodeIssue::InvalidProfile);
            return event(Observation::Kind::SaveRequested,id);
        }
        if(method_name=="logout") {
            Field id_node;
            point(DecodePoint::LogoutChildren);
            if(!scan(service,first_child,[&](Node node,std::string_view key) {
                if(key=="iidx_id") id_node.accept(node);
            })) return fault();
            std::array<char,32> id;
            if(!value(service,id_node,nullptr,id,DecodePoint::LogoutId)) return fault();
            if(!valid_profile_id(id)) return fault(DecodeIssue::InvalidProfile);
            return event(Observation::Kind::LogoutRequested,id);
        }
        return {};
    }

    DecodeOutcome response_event(Node service) {
        Field pcdata;
        point(DecodePoint::ResponseChildren);
        if(!scan(service,first_child,[&](Node node,std::string_view key) {
            if(key=="pcdata") pcdata.accept(node);
        })) return fault();
        // A bare status response cannot establish which request completed.
        if(!pcdata.node) return {};
        if(pcdata.duplicate) return fault(DecodeIssue::Duplicate);
        Field status,fault_field;
        point(DecodePoint::ResponseAttributes);
        if(!scan(service,first_attribute,[&](Node node,std::string_view key) {
            if(key=="status") status.accept(node);
            else if(key=="fault") fault_field.accept(node);
        })) return fault();
        std::array<char,32> status_value,fault_value;
        if(!value(service,status,"status@",status_value,DecodePoint::ResponseStatus)) return fault();
        if(std::strcmp(status_value.data(),"0")!=0) return {};
        // Successful servers may omit fault; an explicit error still rejects the profile.
        if(fault_field.node) {
            if(!value(service,fault_field,"fault@",fault_value,DecodePoint::ResponseFault)) return fault();
            if(std::strcmp(fault_value.data(),"0")!=0) return {};
        }

        Field id_node;
        point(DecodePoint::ProfileAttributes);
        if(!scan(pcdata.node,first_attribute,[&](Node node,std::string_view key) {
            if(key=="id") id_node.accept(node);
        })) return fault();
        std::array<char,32> id;
        if(!value(pcdata.node,id_node,"id@",id,DecodePoint::ProfileId)) return fault();
        if(!valid_profile_id(id)) return fault(DecodeIssue::InvalidProfile);
        return event(Observation::Kind::ProfileLoaded,id);
    }
    DecodeOutcome sdvx_request(Node service) {
        Field method;
        point(DecodePoint::RequestAttributes);
        if(!scan(service,first_attribute,[&](Node node,std::string_view key) {
            if(key=="method") method.accept(node);
        })) return fault();
        std::array<char,32> method_value;
        if(!value(service,method,"method@",method_value,DecodePoint::RequestMethod)) return fault();
        const std::string_view method_name(method_value.data());
        if(method_name!="sv7_load" && method_name!="sv7_save") return {};

        Field refid;
        point(DecodePoint::RequestChildren);
        if(!scan(service,first_child,[&](Node node,std::string_view key) {
            if(key=="refid") refid.accept(node);
        })) return fault();
        std::array<char,32> id;
        if(!value(service,refid,nullptr,id,DecodePoint::RequestRefId)) return fault();
        if(!valid_sdvx_refid(id)) return fault(DecodeIssue::InvalidProfile);
        return event(method_name=="sv7_load" ? Observation::Kind::GetRequested : Observation::Kind::SaveRequested,id);
    }

    DecodeOutcome sdvx_response(Node service) {
        Field code,result;
        point(DecodePoint::ResponseChildren);
        if(!scan(service,first_child,[&](Node node,std::string_view key) {
            if(key=="code") code.accept(node);
            else if(key=="result") result.accept(node);
        })) return fault();
        // Empty acknowledgements and result-only replies do not identify a profile.
        if(!code.node) return {};
        point(DecodePoint::ResponseResult);
        if(!result.node) return fault(DecodeIssue::Missing);
        if(result.duplicate) return fault(DecodeIssue::Duplicate);
        diagnostic_.observed_type=f_.node_type(result.node);
        if(diagnostic_.observed_type!=u8_type) return fault(DecodeIssue::WrongType);
        uint8_t status=0xff;
        diagnostic_.accessor_result=f_.read(result.node,u8_type,&status,sizeof(status));
        if(diagnostic_.accessor_result<0) return fault(DecodeIssue::AccessorFailed);
        if(status!=0) return {};
        std::array<char,32> id;
        if(!value(service,code,nullptr,id,DecodePoint::ProfileId)) return fault();
        if(!valid_profile_id(id)) return fault(DecodeIssue::InvalidProfile);
        // The response's code is not the refid used by subsequent saves.
        return event(Observation::Kind::ProfileLoaded);
    }

    Property property_;
    const AvsFunctions &f_;
    GameKind game_;
    size_t total_{};
    bool target_{};
    DecodeDiagnostic diagnostic_{};
};
} // namespace
const char *decode_point_name(DecodePoint point) noexcept {
    constexpr const char *names[]={"none","accessors","root_search","root_name","services",
        "request_attributes","request_method","get_cid","get_rid","save_id","logout_children",
        "logout_id","response_children","response_attributes","response_status","response_fault",
        "profile_attributes","profile_id","request_children","request_refid","response_result"};
    const auto index=static_cast<size_t>(point);
    return index<std::size(names)?names[index]:"invalid";
}
const char *decode_issue_name(DecodeIssue issue) noexcept {
    constexpr const char *names[]={"none","missing","duplicate","wrong_type","accessor_failed",
        "unterminated","empty","invalid_profile","traversal_limit","avs_error","observer_exception"};
    const auto index=static_cast<size_t>(issue);
    return index<std::size(names)?names[index]:"invalid";
}

DecodeOutcome decode_property(Property property,const AvsFunctions &f,GameKind game) noexcept {
    if(!property || (game!=GameKind::Iidx && game!=GameKind::Sdvx)) return {};
    if(!f.get_error || !f.clear_error || !f.search || !f.traverse || !f.name ||
       !f.node_type || !f.read || !f.refer)
        return {DecodeKind::Fault,{}, {DecodePoint::Accessors,DecodeIssue::Missing}};
    bool initially_clear=false;
    Decoder decoder(property,f,game);
    try {
        if(f.get_error(property)!=0) return {};
        initially_clear=true;
        DecodeOutcome result=decoder.decode();
        const auto error=f.get_error(property);
        // A property without a root and without an AVS error has no protocol
        // tree to observe. Classify it only after the final error check.
        if(error==0 && result.kind==DecodeKind::Fault &&
           result.diagnostic.point==DecodePoint::RootSearch &&
           result.diagnostic.issue==DecodeIssue::Missing) return {};
        if(error!=0) {
            f.clear_error(property);
            if(!decoder.target() && result.kind!=DecodeKind::Fault) return {};
            result.kind=DecodeKind::Fault;
            result.event={};
            result.diagnostic=decoder.diagnostic();
            result.diagnostic.avs_error=error;
            if(result.diagnostic.issue==DecodeIssue::None) result.diagnostic.issue=DecodeIssue::AvsError;
        }
        return result;
    } catch(...) {
        DecodeDiagnostic diagnostic=decoder.diagnostic();
        diagnostic.issue=DecodeIssue::ObserverException;
        if(initially_clear) {
            try {
                diagnostic.avs_error=f.get_error(property);
                if(diagnostic.avs_error!=0) f.clear_error(property);
            } catch(...) {}
        }
        return {DecodeKind::Fault,{},diagnostic};
    }
}
} // namespace cabinet
