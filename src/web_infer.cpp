#include "web_infer.hpp"
#include "web_infer_page.hpp"
#include <json-c/json.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib,"ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <signal.h>
#include <cerrno>
#endif
namespace neo::web {
namespace {
namespace fs=std::filesystem;
#ifdef _WIN32
using Socket=SOCKET;
constexpr Socket invalid_socket=INVALID_SOCKET;
void close_socket(Socket s){closesocket(s);}
#else
using Socket=int;
constexpr Socket invalid_socket=-1;
void close_socket(Socket s){close(s);}
#endif
struct SocketOwner {
  Socket fd=invalid_socket;
  explicit SocketOwner(Socket handle=invalid_socket):fd(handle){}
  ~SocketOwner(){if(fd!=invalid_socket)close_socket(fd);}
  SocketOwner(const SocketOwner&)=delete;
  SocketOwner& operator=(const SocketOwner&)=delete;
};
struct JsonDelete {void operator()(json_object* o)const{if(o)json_object_put(o);}};
using Json=std::unique_ptr<json_object,JsonDelete>;
void send_all(Socket fd,const char* data,std::size_t bytes) {
  while(bytes) {
    const auto size=static_cast<int>((std::min)(bytes,static_cast<std::size_t>(32768)));
#ifdef _WIN32
    const int n=send(fd,data,size,0);
#else
    const int n=send(fd,data,size,MSG_NOSIGNAL);
#endif
    if(n<=0)throw std::runtime_error("Web client connection closed");
    data+=n;bytes-=static_cast<std::size_t>(n);
  }
}
void reply(Socket fd,int status,const std::string& type,const std::string& content){
  std::string h="HTTP/1.1 "+std::to_string(status)+(status==200?" OK":" Error")+"\r\n";
  h+="Content-Type: "+type+"\r\nContent-Length: "+std::to_string(content.size());
  if(status==401)h+="\r\nWWW-Authenticate: Basic realm=\"Goradio\"";
  h+="\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY";
  h+="\r\nContent-Security-Policy: default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; media-src 'self' blob:\r\nConnection: close\r\n\r\n";
  send_all(fd,h.data(),h.size());send_all(fd,content.data(),content.size());
}
struct Scratch {
 fs::path directory;
 Scratch(){
   auto tmp=fs::temp_directory_path();
   std::random_device source;
   for(int attempts=0;attempts<12;++attempts){
     auto name="neo-web-"+std::to_string(source())+"-"+std::to_string(source());
     if(fs::create_directory(tmp/name)){directory=tmp/name;break;}
   }
   if(directory.empty())throw std::runtime_error("Cannot allocate temporary audio directory");
#ifndef _WIN32
   fs::permissions(directory,fs::perms::owner_all,fs::perm_options::replace);
#endif
 }
 ~Scratch(){
   // LibTorch's bundled std::filesystem::remove_all can crash under the
   // combined LibTorch/ONNX Runtime build. Only remove our known filenames;
   // never recurse through a directory or follow an attacker-created symlink.
   for(const auto* name:{"speech.wav"}){
#ifdef _WIN32
     DeleteFileW((directory/name).c_str());
#else
     const auto file=(directory/name).string();::unlink(file.c_str());
#endif
   }
#ifdef _WIN32
   RemoveDirectoryW(directory.c_str());
#else
   const auto dir=directory.string();::rmdir(dir.c_str());
#endif
 }
};
std::string request_text(const std::string& body){
  if(body.empty()||body.size()>8192)throw std::invalid_argument("JSON text body exceeds 8 KiB");
  Json tok(json_tokener_parse(body.c_str()));
  if(!tok||json_object_get_type(tok.get())!=json_type_object)
    throw std::invalid_argument("Expected JSON object with text");
  json_object* val=nullptr;
  if(!json_object_object_get_ex(tok.get(),"text",&val)||!val||json_object_get_type(val)!=json_type_string)
    throw std::invalid_argument("Expected string text");
  const char* chars=json_object_get_string(val);
  std::string text(chars,static_cast<std::size_t>(json_object_get_string_len(val)));
  if(text.empty()||text.size()>4096||text.find('\0')!=std::string::npos)
    throw std::invalid_argument("Text must contain 1..4096 bytes without NUL");
  return text;
}
std::string base64(const std::string& str){
  static const char* digits="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;int val=0,bits=-6;
  for(unsigned char c:str){val=(val<<8)|c;bits+=8;while(bits>=0){out+=digits[(val>>bits)&63];bits-=6;}}
  if(bits>-6)out+=digits[((val<<8)>>(bits+8))&63];
  while(out.size()%4)out+='=';
  return out;
}
bool equal_constant(const std::string& a,const std::string& b){
  unsigned v=static_cast<unsigned>(a.size()^b.size());
  for(std::size_t i=0;i<(std::max)(a.size(),b.size());++i)
    v|=(i<a.size()?static_cast<unsigned char>(a[i]):0)^(i<b.size()?static_cast<unsigned char>(b[i]):0);
  return v==0;
}
void serve_one(Socket peer,int port,const Synthesize& synth,const std::string& engine,const Options& settings){
  constexpr std::size_t limit_header=8192;
  std::string req;std::array<char,4096> buf{};
  std::size_t header_end=std::string::npos;
  while((header_end=req.find("\r\n\r\n"))==std::string::npos){
    if(req.size()>limit_header)throw std::invalid_argument("Headers exceed limit");
    int n=recv(peer,buf.data(),static_cast<int>(buf.size()),0);
    if(n<=0)throw std::runtime_error("HTTP request incomplete");
    req.append(buf.data(),static_cast<std::size_t>(n));
  }
  if(header_end>limit_header)throw std::invalid_argument("Headers exceed limit");
  const auto header=req.substr(0,header_end); const auto body_start=header_end+4;
  std::istringstream lines(header);std::string first,method,path,version;
  std::getline(lines,first);std::istringstream firstline(first);firstline>>method>>path>>version;
  if(version!="HTTP/1.1"||path.size()>128||(method!="GET"&&method!="POST"))
    throw std::invalid_argument("Bad HTTP method/path");
  std::string line,host,origin,ctype,auth;std::size_t content_length=0;bool has_length=false;
  while(std::getline(lines,line)){
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    auto split=line.find(':');if(split==std::string::npos)throw std::invalid_argument("Malformed HTTP header");
    auto key=line.substr(0,split),value=line.substr(split+1);
    for(char& c:key)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while(!value.empty()&&value.front()==' ')value.erase(value.begin());
    if(key=="host")host=value;else if(key=="origin")origin=value;
    else if(key=="authorization")auth=value;
    else if(key=="content-type")ctype=value;
    else if(key=="transfer-encoding")throw std::invalid_argument("Chunked encoding not supported");
    else if(key=="content-length"){
      if(has_length||value.empty()||value.size()>8)throw std::invalid_argument("Invalid Content-Length");
      for(char c:value)if(c<'0'||c>'9')throw std::invalid_argument("Invalid Content-Length");
      has_length=true;content_length=static_cast<std::size_t>(std::stoul(value));
    }
  }
  const auto local1="127.0.0.1:"+std::to_string(port),local2="localhost:"+std::to_string(port);
  const bool gradio=settings.shared&&host.size()>12&&host.substr(host.size()-12)==".gradio.live"&&
    host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.")==std::string::npos;
  if(host!=local1&&host!=local2&&!gradio)throw std::invalid_argument("Untrusted HTTP Host");
  if(method=="GET"&&path=="/")return reply(peer,200,"text/html; charset=utf-8",
                                                studio_html(engine,!settings.user.empty()));
  if(!settings.user.empty()&&!equal_constant(auth,"Basic "+base64(settings.user+":"+settings.password)))
    return reply(peer,401,"application/json","{\"error\":\"authentication_required\"}");
  if(method=="GET"&&path=="/api/health")return reply(peer,200,"application/json","{\"ok\":true,\"engine\":\"neo-native\"}");
  if(method!="POST"||path!="/api/synthesize")
    return reply(peer,404,"application/json","{\"error\":\"not_found\"}");
  if(!origin.empty()&&origin!="http://"+local1&&origin!="http://"+local2&&
       !(gradio&&origin=="https://"+host))throw std::invalid_argument("Cross-origin POST rejected");
  if(ctype.rfind("application/json",0)!=0||!has_length||content_length>8192)
    throw std::invalid_argument("Expected bounded JSON Content-Length");
  std::string body=req.substr(body_start);if(body.size()>content_length)body.resize(content_length);
  while(body.size()<content_length){
    const auto remaining=(std::min)(content_length-body.size(),buf.size());
    const int n=recv(peer,buf.data(),static_cast<int>(remaining),0);
    if(n<=0)throw std::runtime_error("Incomplete JSON request");
    body.append(buf.data(),static_cast<std::size_t>(n));
  }
  Scratch temporary;const auto output=temporary.directory/"speech.wav";
  {
    const auto text=request_text(body);synth(text,output.string());
    std::error_code ec;const auto n=fs::file_size(output,ec);
    if(ec||n<44||n>32*1024*1024)throw std::runtime_error("WAV outside supported size");
    std::ifstream in(output,std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(in)),{});
    if(data.size()!=n||data.compare(0,4,"RIFF")!=0||data.compare(8,4,"WAVE")!=0)
      throw std::runtime_error("Synthesized file is not RIFF/WAVE");
    return reply(peer,200,"audio/wav",data);
  }
}
} // namespace
namespace {
void start_native_share(int port){
#ifdef _WIN32
  std::wstring path(32768,L'\0');
  const DWORD len=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
  if(!len||len>=path.size())throw std::runtime_error("Cannot locate Neo TTS path for share");
  path.resize(len);
  const auto helper=std::filesystem::path(path).parent_path()/L"neo-share.exe";
  if(!std::filesystem::is_regular_file(helper))
    throw std::runtime_error("Missing neo-share.exe beside Neo TTS. Bundle native Gradio tunnel helper");
  std::wstring command=L"\""+helper.wstring()+L"\" --port "+std::to_wstring(port);
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  if(!CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process))
    throw std::runtime_error("Cannot start native Gradio tunnel");
  CloseHandle(process.hThread);CloseHandle(process.hProcess);
#else
  std::array<char,4096> exe{};const ssize_t size=readlink("/proc/self/exe",exe.data(),exe.size()-1);
  if(size<=0)throw std::runtime_error("Cannot find Neo executable for share");
  auto helper=std::filesystem::path(std::string(exe.data(),static_cast<std::size_t>(size))).parent_path()/"neo-share";
  if(const char* alt=std::getenv("NEO_SHARE_BIN");alt&&*alt)helper=alt;
  if(!std::filesystem::is_regular_file(helper))
    throw std::runtime_error("Missing neo-share native executable; run tools/neo-share/build.sh");
  const pid_t parent=getpid(),child=fork();
  if(child<0)throw std::runtime_error("Cannot start share child process");
  if(child==0){
    prctl(PR_SET_PDEATHSIG,SIGTERM);
    if(getppid()!=parent)_exit(1);
    const auto text=std::to_string(port),app=helper.string();
    execl(app.c_str(),app.c_str(),"--port",text.c_str(),static_cast<char*>(nullptr));
    _exit(127);
  }
#endif
  std::cout<<"NEO_SHARE_STARTING via Gradio native FRP (TLS + authenticated WebUI)"<<std::endl;
}
} // namespace
int serve(int port,const std::string& engine,const Synthesize& synth,const Options& settings){
  if(port<1024||port>65535)throw std::invalid_argument("--port must be 1024..65535");
#ifdef _WIN32
  WSADATA ws{};if(WSAStartup(MAKEWORD(2,2),&ws))throw std::runtime_error("Winsock startup failed");
#endif
  SocketOwner listener(socket(AF_INET,SOCK_STREAM,0));
  if(listener.fd==invalid_socket)throw std::runtime_error("Cannot create HTTP listener");
  const int reuse=1;
#ifdef _WIN32
  setsockopt(listener.fd,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&reuse),sizeof(reuse));
#else
  setsockopt(listener.fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
#endif
  sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(static_cast<uint16_t>(port));
  local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(bind(listener.fd,reinterpret_cast<sockaddr*>(&local),sizeof(local))!=0||listen(listener.fd,4)!=0)
    throw std::runtime_error("Cannot bind HTTP server to 127.0.0.1:"+std::to_string(port));
  std::cout<<"NEO_WEB_READY url=http://127.0.0.1:"<<port<<" engine="<<engine<<std::endl;
  if(settings.shared)start_native_share(port);
  for(;;){
    SocketOwner peer(accept(listener.fd,nullptr,nullptr));
    if(peer.fd==invalid_socket)continue;
#ifdef _WIN32
    DWORD ms=15000;
    setsockopt(peer.fd,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&ms),sizeof(ms));
#else
    timeval timeout{15,0};
    setsockopt(peer.fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
#endif
    try{serve_one(peer.fd,port,synth,engine,settings);}
    catch(const std::exception& ex){
      std::cerr<<"Neo Web: "<<ex.what()<<"\n";
      try{reply(peer.fd,400,"application/json","{\"error\":\"synthesis_failed_or_invalid_request\"}");}
      catch(...){}
    }
  }
}
} // namespace neo::web
