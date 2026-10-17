#include "open_format/oneo.hpp"
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
int oneo_compiler_cli(int argc,char**argv){
 try{
  if(argc==2&&std::string(argv[1])=="--help"){
   std::cout<<"oneo-compiler build --source voice.onnx --out voice.oneo [--config voice.onnx.json]\n"
    "  [--name NAME] [--author AUTHOR] [--description TEXT] [--cover image.png]\n"
    "  [--license LICENSE] [--version VERSION] [--url https://example.com]\n"
    "oneo-compiler verify --model voice.oneo\n";
   return 0;
  }
  if(argc==4&&std::string(argv[1])=="verify"&&std::string(argv[2])=="--model"){
   auto model=openneo::load(argv[3]);
   std::cout<<"ONEO_VALID format=v"<<model.format_version<<" onnx_bytes="<<model.onnx.size()
            <<" config_bytes="<<model.config.size()<<" cover_bytes="<<model.cover.size();
   if(!model.metadata.empty())std::cout<<" metadata="<<model.metadata;
   std::cout<<"\n";return 0;
  }
  if(argc<6||std::string(argv[1])!="build"||argc%2!=0)throw std::invalid_argument("Run oneo-compiler --help");
  std::map<std::string,std::string> opts;
  for(int i=2;i<argc;i+=2){
   const std::string key=argv[i];
   if(key!="--source"&&key!="--config"&&key!="--out"&&key!="--name"&&key!="--author"&&
      key!="--description"&&key!="--cover"&&key!="--license"&&key!="--version"&&key!="--url")
     throw std::invalid_argument("Unknown ONEO option: "+key);
   if(!opts.emplace(key,argv[i+1]).second)throw std::invalid_argument("Repeated ONEO option");
  }
  if(!opts.count("--source")||!opts.count("--out"))throw std::invalid_argument("ONEO requires --source and --out");
  const auto source=std::filesystem::path(opts.at("--source"));
  openneo::Metadata meta;
  for(auto& v:opts){
   if(v.first=="--name")meta.name=v.second;
   else if(v.first=="--author")meta.author=v.second;
   else if(v.first=="--description")meta.description=v.second;
   else if(v.first=="--license")meta.license=v.second;
   else if(v.first=="--version")meta.version=v.second;
   else if(v.first=="--url")meta.url=v.second;
   else if(v.first=="--cover")meta.cover=v.second;
  }
  openneo::pack(source,opts.count("--config")?std::filesystem::path(opts.at("--config")):
                 std::filesystem::path(source.string()+".json"),opts.at("--out"),meta);
  std::cout<<"ONEO_BUILT "<<opts.at("--out")<<"\n";return 0;
 }catch(const std::exception& e){std::cerr<<"ONEO: "<<e.what()<<"\n";return 1;}
}


#ifdef _WIN32
int wmain(int argc,wchar_t** wide){
  try{
    std::vector<std::string> converted;
    converted.reserve(static_cast<std::size_t>(argc));
    for(int i=0;i<argc;++i){
      const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,nullptr,0,nullptr,nullptr);
      if(size<=0)throw std::runtime_error("Invalid Unicode argument");
      std::string arg(static_cast<std::size_t>(size),'\0');
      if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,arg.data(),size,nullptr,nullptr)!=size)
        throw std::runtime_error("Invalid Unicode argument");
      arg.pop_back();
      converted.push_back(std::move(arg));
    }
    std::vector<char*> arguments;
    for(auto& value:converted)arguments.push_back(value.data());
    return oneo_compiler_cli(argc,arguments.data());
  }catch(const std::exception& e){
    std::cerr<<"ONEO: "<<e.what()<<"\n";
    return 1;
  }
}
#else
int main(int argc,char** argv){return oneo_compiler_cli(argc,argv);}
#endif
