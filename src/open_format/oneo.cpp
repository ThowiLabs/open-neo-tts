#include "open_format/oneo.hpp"
#include <openssl/evp.h>
#include <json-c/json.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <stdexcept>
namespace openneo {
namespace {
using Bytes=std::vector<unsigned char>;
constexpr char domain1[]="OPEN-NEO-TTS-ONEO-1", domain2[]="OPEN-NEO-TTS-ONEO-2";
constexpr std::uint64_t max_model=1024ULL*1024*1024;
constexpr std::uint32_t max_config=1024*1024,max_meta=2*1024*1024,max_cover=8*1024*1024;
struct JDel{void operator()(json_object* p)const{if(p)json_object_put(p);}};
using Json=std::unique_ptr<json_object,JDel>;
void put32(Bytes& x,std::uint32_t n){for(unsigned k=0;k<32;k+=8)x.push_back(static_cast<unsigned char>(n>>k));}
void put64(Bytes& x,std::uint64_t n){for(unsigned k=0;k<64;k+=8)x.push_back(static_cast<unsigned char>(n>>k));}
std::uint32_t get32(const Bytes& x,size_t pos){return std::uint32_t(x[pos])|(std::uint32_t(x[pos+1])<<8)|
    (std::uint32_t(x[pos+2])<<16)|(std::uint32_t(x[pos+3])<<24);}
std::uint64_t get64(const Bytes& x,size_t pos){return get32(x,pos)|(std::uint64_t(get32(x,pos+4))<<32);}
Bytes read_file(const std::filesystem::path& p,std::uint64_t max){
  std::error_code ec;auto n=std::filesystem::file_size(p,ec);
  if(ec||!std::filesystem::is_regular_file(p)||!n||n>max)throw std::runtime_error("ONEO input missing or too large");
  std::ifstream in(p,std::ios::binary);Bytes result(static_cast<size_t>(n));
  if(!in||!in.read(reinterpret_cast<char*>(result.data()),result.size()))throw std::runtime_error("Cannot read ONEO input");
  return result;
}
Json parse(const std::string& s){
  json_tokener* tok=json_tokener_new();
  if(!tok)throw std::bad_alloc();
  json_object* parsed=json_tokener_parse_ex(tok,s.data(),static_cast<int>(s.size()));
  const auto err=json_tokener_get_error(tok);const auto offset=json_tokener_get_parse_end(tok);
  json_tokener_free(tok);Json object(parsed);
  if(err!=json_tokener_success||offset!=s.size()||!object||
      json_object_get_type(object.get())!=json_type_object)throw std::runtime_error("Malformed ONEO JSON");
  return object;
}
void field(json_object* obj,const char* name,const std::string& value){
  if(value.size()>8192||value.find('\0')!=std::string::npos)throw std::runtime_error("ONEO metadata too long");
  json_object_object_add(obj,name,json_object_new_string_len(value.data(),static_cast<int>(value.size())));
}
void validate_cover(const Bytes& x){
  if(x.empty())return;
  bool png=x.size()>=12&&x[0]==0x89&&x[1]=='P'&&x[2]=='N'&&x[3]=='G';
  bool jpg=x.size()>=12&&x[0]==0xff&&x[1]==0xd8&&x[2]==0xff;
  bool webp=x.size()>=12&&std::equal(x.begin(),x.begin()+4,"RIFF")&&
    std::equal(x.begin()+8,x.begin()+12,"WEBP");
  if(!png&&!jpg&&!webp)throw std::runtime_error("ONEO cover must be PNG, JPEG or WebP");
}
std::array<unsigned char,32> hash(const Bytes& x,bool v2){
  const auto h=v2?64:56,prefix=v2?32:24;
  const auto domain=v2?domain2:domain1;
  const auto length=v2?sizeof(domain2):sizeof(domain1);
  if(x.size()<h)throw std::runtime_error("Incomplete ONEO header");
  using Ctx=std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)>;
  Ctx ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);
  if(!ctx||EVP_DigestInit_ex(ctx.get(),EVP_sha256(),nullptr)!=1||
     EVP_DigestUpdate(ctx.get(),domain,length)!=1||
     EVP_DigestUpdate(ctx.get(),x.data(),prefix)!=1||
     EVP_DigestUpdate(ctx.get(),x.data()+h,x.size()-h)!=1)
    throw std::runtime_error("ONEO hash initialization failed");
  std::array<unsigned char,32> out{};unsigned int n=0;
  if(EVP_DigestFinal_ex(ctx.get(),out.data(),&n)!=1||n!=32)throw std::runtime_error("ONEO hash failed");
  return out;
}
std::string make_card(const Metadata& m,const std::string& config,const std::filesystem::path& source){
  Json card(json_object_new_object());
  field(card.get(),"name",m.name.empty()?source.stem().string():m.name);
  field(card.get(),"author",m.author);
  field(card.get(),"description",m.description);
  field(card.get(),"license",m.license);
  field(card.get(),"version",m.version);
  if(!m.url.empty()&&m.url.rfind("http://",0)!=0&&m.url.rfind("https://",0)!=0)
    throw std::runtime_error("ONEO URL must use http:// or https://");
  field(card.get(),"url",m.url);
  auto cfg=parse(config);json_object *espeak=nullptr,*voice=nullptr;
  if(json_object_object_get_ex(cfg.get(),"espeak",&espeak)&&json_object_get_type(espeak)==json_type_object&&
     json_object_object_get_ex(espeak,"voice",&voice)&&json_object_get_type(voice)==json_type_string)
    field(card.get(),"language",json_object_get_string(voice));
  const auto text=std::string(json_object_to_json_string_ext(card.get(),JSON_C_TO_STRING_PLAIN));
  if(text.size()>max_meta)throw std::runtime_error("ONEO metadata exceeds size limit");
  return text;
}
}
Model load(const std::filesystem::path& filename){
  auto x=read_file(filename,64ULL+max_config+max_meta+max_cover+max_model);
  if(x.size()<56)throw std::runtime_error("Invalid ONEO header");
  const bool v1=std::equal(x.begin(),x.begin()+8,"ONEOTTS1");
  const bool v2=std::equal(x.begin(),x.begin()+8,"ONEOTTS2");
  if((!v1&&!v2)||get32(x,8)!=(v2?2U:1U))throw std::runtime_error("Unsupported ONEO version");
  const std::uint64_t h=v2?64:56;
  if(x.size()<h)throw std::runtime_error("Truncated ONEO v2 header");
  const auto cfg=get32(x,12);const auto onnx=get64(x,16);
  const auto meta=v2?get32(x,24):0U,cover=v2?get32(x,28):0U;
  if(!cfg||cfg>max_config||onnx<16||onnx>max_model||meta>max_meta||cover>max_cover||
     (v2&&!meta)||x.size()!=h+std::uint64_t(cfg)+meta+cover+onnx)
    throw std::runtime_error("Invalid ONEO section sizes");
  const auto digest=hash(x,v2);
  if(!std::equal(digest.begin(),digest.end(),x.begin()+(v2?32:24)))
    throw std::runtime_error("ONEO checksum mismatch");
  Model result;result.format_version=v2?2:1;
  auto ptr=x.begin()+static_cast<std::ptrdiff_t>(h);
  result.config.assign(ptr,ptr+cfg);ptr+=cfg;
  (void)parse(result.config);
  if(v2){
    result.metadata.assign(ptr,ptr+meta);ptr+=meta;
    auto card=parse(result.metadata);
    for(const char* key:{"name","author","description","license","version","url","language"}){
      json_object* v=nullptr;
      if(json_object_object_get_ex(card.get(),key,&v)&&json_object_get_type(v)!=json_type_string)
        throw std::runtime_error("ONEO model card field type invalid");
    }
    result.cover.assign(ptr,ptr+cover);ptr+=cover;validate_cover(result.cover);
  }
  result.onnx.assign(ptr,x.end());
  return result;
}
void pack(const std::filesystem::path& onnx,const std::filesystem::path& cfg,
          const std::filesystem::path& output,const Metadata& meta){
  if(onnx.extension()!=".onnx"||output.extension()!=".oneo")throw std::runtime_error("ONEO requires .onnx input and .oneo output");
  if(std::filesystem::exists(output))throw std::runtime_error("ONEO destination exists");
  const auto data=read_file(onnx,max_model),config=read_file(cfg,max_config);
  const std::string cfg_text(config.begin(),config.end());
  const auto card=make_card(meta,cfg_text,onnx);
  Bytes image;if(!meta.cover.empty())image=read_file(meta.cover,max_cover);
  validate_cover(image);
  Bytes output_bytes;output_bytes.reserve(64+config.size()+card.size()+image.size()+data.size());
  output_bytes.insert(output_bytes.end(),{'O','N','E','O','T','T','S','2'});
  put32(output_bytes,2);put32(output_bytes,static_cast<std::uint32_t>(config.size()));
  put64(output_bytes,data.size());put32(output_bytes,static_cast<std::uint32_t>(card.size()));
  put32(output_bytes,static_cast<std::uint32_t>(image.size()));
  output_bytes.resize(64,0);
  output_bytes.insert(output_bytes.end(),config.begin(),config.end());
  output_bytes.insert(output_bytes.end(),card.begin(),card.end());
  output_bytes.insert(output_bytes.end(),image.begin(),image.end());
  output_bytes.insert(output_bytes.end(),data.begin(),data.end());
  const auto digest=hash(output_bytes,true);
  std::copy(digest.begin(),digest.end(),output_bytes.begin()+32);
  const auto tmp=output.string()+".part";
  if(std::filesystem::exists(tmp))throw std::runtime_error("ONEO temporary output exists");
  try{
    std::ofstream out(tmp,std::ios::binary);
    if(!out||!out.write(reinterpret_cast<const char*>(output_bytes.data()),output_bytes.size()))
      throw std::runtime_error("Cannot write ONEO");
    out.close();if(!out)throw std::runtime_error("Incomplete ONEO write");
    std::filesystem::rename(tmp,output);
  }catch(...){std::error_code ec;std::filesystem::remove(tmp,ec);throw;}
}
}
