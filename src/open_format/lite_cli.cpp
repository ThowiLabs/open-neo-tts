#include "open_format/oneo.hpp"
#include "phonemes.hpp"
#include "vits_text_chunker.hpp"
#include "web_infer.hpp"
#include <onnxruntime_cxx_api.h>
#include <json-c/json.h>
#include <algorithm>
#include <cstdlib>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // Prevent windows.h from replacing std::max/std::min with macros.
#endif
#include <windows.h>
#include <cstdlib>
#endif

namespace {
namespace fs=std::filesystem;
struct JsonDelete{void operator()(json_object* x)const{if(x)json_object_put(x);}};
using Json=std::unique_ptr<json_object,JsonDelete>;
std::string required(const std::map<std::string,std::string>& args,const char* name){
  auto it=args.find(name);
  if(it==args.end()||it->second.empty())throw std::invalid_argument(std::string("Missing ")+name);
  return it->second;
}
std::string read_file(const fs::path& path,std::uint64_t limit){
  std::error_code ec;const auto size=fs::file_size(path,ec);
  if(ec||size==0||size>limit)throw std::runtime_error("Text/config file missing or too large");
  std::ifstream in(path,std::ios::binary);
  if(!in)throw std::runtime_error("Cannot open text/config file");
  std::string data(static_cast<std::size_t>(size),'\0');
  if(!in.read(data.data(),static_cast<std::streamsize>(data.size())))
    throw std::runtime_error("Cannot read text/config file");
  return data;
}
Json parse_config(const std::string& data){
  auto* tok=json_tokener_new();if(!tok)throw std::bad_alloc();
  json_object* raw=json_tokener_parse_ex(tok,data.data(),static_cast<int>(data.size()));
  const auto status=json_tokener_get_error(tok);
  const auto end=json_tokener_get_parse_end(tok);
  json_tokener_free(tok);Json result(raw);
  if(status!=json_tokener_success||end!=data.size()||!result||
     json_object_get_type(result.get())!=json_type_object)
    throw std::runtime_error("Invalid model config JSON");
  return result;
}
json_object* object(json_object* obj,const char* key){
  json_object* o=nullptr;
  if(!json_object_object_get_ex(obj,key,&o)||!o||json_object_get_type(o)!=json_type_object)
    throw std::runtime_error(std::string("Missing model config object ")+key);
  return o;
}
double number(json_object* obj,const char* key){
  json_object* n=nullptr;
  if(!json_object_object_get_ex(obj,key,&n)||!n||
      (json_object_get_type(n)!=json_type_double&&json_object_get_type(n)!=json_type_int))
    throw std::runtime_error(std::string("Missing numeric config ")+key);
  const double v=json_object_get_double(n);
  if(!std::isfinite(v))throw std::runtime_error("Invalid numeric config");
  return v;
}
struct Config {int rate;std::array<float,3> scales;};
Config audio_config(const std::string& config){
  Json root=parse_config(config);
  const auto rate=number(object(root.get(),"audio"),"sample_rate");
  if(rate<8000||rate>48000||std::floor(rate)!=rate)throw std::runtime_error("Invalid audio rate");
  auto* inf=object(root.get(),"inference");
  const auto noise=number(inf,"noise_scale"),length=number(inf,"length_scale"),
             noise_w=number(inf,"noise_w");
  if(noise<0||noise>2||length<=0||length>4||noise_w<0||noise_w>2)
    throw std::runtime_error("Unsupported VITS inference scales");
  return {static_cast<int>(rate),{static_cast<float>(noise),static_cast<float>(length),static_cast<float>(noise_w)}};
}
struct WavWriter {
  fs::path path,tmp;
  std::ofstream file;
  std::uint64_t samples=0;
  const int rate;
  explicit WavWriter(const fs::path& output,int sample_rate)
    :path(output),tmp(output.string()+".part"),rate(sample_rate){
    if(fs::exists(path)||fs::exists(tmp))throw std::runtime_error("WAV output already exists");
    file.open(tmp,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot create WAV file");
    std::array<char,44> zero{};file.write(zero.data(),zero.size());
  }
  ~WavWriter(){file.close();std::error_code ec;fs::remove(tmp,ec);}
  void u16(std::uint16_t v){file.put(static_cast<char>(v));file.put(static_cast<char>(v>>8));}
  void u32(std::uint32_t v){u16(static_cast<std::uint16_t>(v));u16(static_cast<std::uint16_t>(v>>16));}
  void append(const float* data,std::size_t n){
    if(n>static_cast<std::uint64_t>(rate)*1800-samples)
      throw std::runtime_error("WAV exceeds 30 minutes");
    float peak=0.01f;
    for(std::size_t i=0;i<n;++i){if(!std::isfinite(data[i]))throw std::runtime_error("Non-finite audio");peak=std::max(peak,std::abs(data[i]));}
    const auto scale=32767.0f/peak;
    for(std::size_t i=0;i<n;++i){
      const auto val=static_cast<std::int16_t>(std::clamp(std::round(data[i]*scale),-32768.0f,32767.0f));
      u16(static_cast<std::uint16_t>(val));
    }
    samples+=n;
    if(!file)throw std::runtime_error("WAV write failed");
  }
  void silence(int ms){
    const auto count=static_cast<std::uint64_t>(rate)*ms/1000;
    if(count>static_cast<std::uint64_t>(rate)*1800-samples)throw std::runtime_error("WAV exceeds 30 minutes");
    for(std::uint64_t i=0;i<count;++i)u16(0);
    samples+=count;
  }
  void finish(){
    if(samples==0||samples*2>std::numeric_limits<std::uint32_t>::max()-36)
      throw std::runtime_error("Invalid WAV length");
    file.seekp(0);
    file.write("RIFF",4);u32(static_cast<std::uint32_t>(samples*2+36));
    file.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(rate);u32(rate*2);u16(2);u16(16);
    file.write("data",4);u32(static_cast<std::uint32_t>(samples*2));
    file.close();
    if(!file)throw std::runtime_error("WAV finalization failed");
    fs::rename(tmp,path);
  }
};
#ifdef _WIN32
std::string utf8(const wchar_t* str){
  const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,str,-1,nullptr,0,nullptr,nullptr);
  if(size<=0)throw std::runtime_error("Invalid Windows argument");
  std::string out(static_cast<std::size_t>(size),'\0');
  if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,str,-1,out.data(),size,nullptr,nullptr)!=size)
    throw std::runtime_error("UTF-8 conversion failed");
  out.pop_back();return out;
}
void data_path(){
  if(const auto* e=std::getenv("NEO_ESPEAK_DATA");e&&*e)return;
  std::wstring path(32768,L'\0');
  auto n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
  if(n==0||n>=path.size())throw std::runtime_error("Executable path unavailable");
  path.resize(n);
  auto root=fs::path(path).parent_path();
  if(fs::is_directory(root/L"espeak-ng-data"))_wputenv_s(L"NEO_ESPEAK_DATA",root.c_str());
}
#endif
int neo_lite_cli_impl(const std::vector<std::string>& argv){
  // Packaging smoke: initialize real eSpeak without a private voice model.
  if((argv.size()==2||argv.size()==3)&&argv[1]=="--check-espeak"){
    neo::phonemes::VoiceConfig voice;
    voice.language="es";
    voice.ids={{"^",{1}},{"_",{0}},{"$",{2}}};
    const auto result=neo::phonemes::from_text(
        argv.size()==3?argv[2]:"Hola, ¿cómo estás?",voice,"",false);
    if(result.ipa.empty())throw std::runtime_error("eSpeak returned no phonemes");
    std::cout<<"ESPEAK_READY\n";
    return 0;
  }
  if(argv.size()==4&&argv[1]=="info"&&argv[2]=="--model"){
    if(fs::path(argv[3]).extension()==".oneo"){
      const auto card=openneo::load(argv[3]);
      std::cout<<"Open Neo ONEO v"<<card.format_version<<": ONNX bytes="<<card.onnx.size()<<" config bytes="<<card.config.size()<<"\\n";
      return 0;
    }
    throw std::runtime_error("Open Neo Lite info supports .oneo only");
  }
  if(argv.size()==2&&argv[1]=="--help"){
    std::cout<<"open-neo-lite --check-espeak ['texto español']\n"
             <<"open-neo-lite info --model voice.oneo\n"
             <<"open-neo-lite infer --model voice.onnx|voice.oneo [--config voice.onnx.json]"
             <<" [--device cpu|cuda] --text 'Hola' --out audio.wav\n"
             <<"--text-file UTF8.txt is also supported. ONEO packages include their voice config.\n"
             <<"open-neo-lite serve --model voice.onnx|voice.oneo [--port 7865] [--device cpu|cuda] [--share]\n"
             <<"  [--auth-user USER --auth-password-env ENV_NAME]\n";
    return 0;
  }
  if(argv.size()>=4&&argv[1]=="serve"){
    std::map<std::string,std::string> opts;neo::web::Options webopts;
    for(std::size_t i=2;i<argv.size();++i){
      const auto& k=argv[i];
      if(k=="--share"){if(webopts.shared)throw std::invalid_argument("Duplicate --share");webopts.shared=true;continue;}
      if(i+1>=argv.size())throw std::invalid_argument("Missing serve value");
      if(k!="--model"&&k!="--config"&&k!="--port"&&k!="--device"&&k!="--auth-user"&&k!="--auth-password-env")
        throw std::invalid_argument("Unknown serve option: "+k);
      if(!opts.emplace(k,argv[++i]).second)throw std::invalid_argument("Duplicate serve option: "+k);
    }
    const auto model=required(opts,"--model");
    if(!fs::is_regular_file(model))throw std::invalid_argument("Web voice model does not exist");
    int port=7865;
    if(opts.count("--port")){
      std::size_t used=0;port=std::stoi(opts.at("--port"),&used);
      if(used!=opts.at("--port").size())throw std::invalid_argument("Invalid web port");
    }
    if(opts.count("--auth-user"))webopts.user=opts.at("--auth-user");
    if(opts.count("--auth-password-env")){
      const char* pass=std::getenv(opts.at("--auth-password-env").c_str());
      if(!pass||!*pass)throw std::invalid_argument("Authentication env variable is empty/missing");
      webopts.password=pass;
    }
    if(webopts.user.empty()!=webopts.password.empty())
      throw std::invalid_argument("Specify --auth-user and --auth-password-env together");
    if(webopts.shared&&(webopts.user.empty()||webopts.password.size()<12))
      throw std::invalid_argument("--share requires username and 12+ char password");
    return neo::web::serve(port,"Open Neo Lite",[model,opts](const std::string& text,const std::string& output){
      std::vector<std::string> arguments{"open-neo-lite","infer","--model",model,"--text",text,"--out",output};
      if(opts.count("--config")){
        arguments.push_back("--config");arguments.push_back(opts.at("--config"));
      }
      if(opts.count("--device")){
        arguments.push_back("--device");arguments.push_back(opts.at("--device"));
      }
      if(neo_lite_cli_impl(arguments)!=0)throw std::runtime_error("Open Neo Lite synthesis failed");
    },webopts);
  }
  if(argv.size()<4||argv[1]!="infer"||argv.size()%2!=0)
    throw std::invalid_argument("Run open-neo-lite --help");
  std::map<std::string,std::string> args;
  for(std::size_t i=2;i<argv.size();i+=2){
    const auto& key=argv[i];
    if(key!="--model"&&key!="--config"&&key!="--text"&&key!="--text-file"&&
       key!="--out"&&key!="--device")throw std::invalid_argument("Unknown option: "+key);
    if(!args.emplace(key,argv[i+1]).second)throw std::invalid_argument("Duplicate option: "+key);
  }
  const fs::path model=required(args,"--model"),output=required(args,"--out");
  std::string text;
  if(args.count("--text")&&args.count("--text-file"))throw std::invalid_argument("Use either --text or --text-file");
  if(args.count("--text-file"))text=read_file(args.at("--text-file"),65536);
  else text=required(args,"--text");
  if(text.empty()||text.size()>65536)throw std::invalid_argument("Speech text must contain 1..65536 UTF-8 bytes");
  Ort::Env env(ORT_LOGGING_LEVEL_WARNING,"open-neo-lite");
  Ort::SessionOptions options;
  options.SetIntraOpNumThreads(2);
  options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
  const auto device=args.count("--device")?args.at("--device"):"cpu";
  if(device=="cuda"){
    const auto providers=Ort::GetAvailableProviders();
    if(std::find(providers.begin(),providers.end(),"CUDAExecutionProvider")==providers.end())
      throw std::runtime_error("CUDAExecutionProvider is missing; use the GPU package with CUDA/cuDNN or --device cpu");
    OrtCUDAProviderOptions cuda_options{};
    cuda_options.device_id=0;
    options.AppendExecutionProvider_CUDA(cuda_options);
  }else if(device!="cpu"){
    throw std::invalid_argument("Invalid --device (expected cpu or cuda)");
  }
  std::unique_ptr<Ort::Session> session;
  std::string voice_json;
  std::vector<unsigned char> ort_storage;
  if(model.extension()==".oneo"){
    if(args.count("--config"))throw std::invalid_argument("ONEO includes its config, do not pass --config");
    auto payload=openneo::load(model);
    voice_json=std::move(payload.config);
    ort_storage=std::move(payload.onnx);
    session=std::make_unique<Ort::Session>(env,ort_storage.data(),ort_storage.size(),options);
  }else if(model.extension()==".onnx"){
    voice_json=read_file(args.count("--config")?fs::path(args.at("--config")):fs::path(model.string()+".json"),1024*1024);
    session=std::make_unique<Ort::Session>(env,model.c_str(),options);
  }else throw std::invalid_argument("Open Neo Lite supports .onnx or .oneo");
  const auto config=audio_config(voice_json);
  const auto voice=neo::phonemes::read_voice_config_json(voice_json);
  if(session->GetInputCount()!=3||session->GetOutputCount()!=1)
    throw std::runtime_error("Only single-speaker Piper VITS ONNX is currently supported");
  auto chunks=neo::vits::split_text_for_speech(text,140);
  std::vector<std::vector<int64_t>> phonemes;
  for(const auto& chunk:chunks){
    const auto result=neo::phonemes::from_text(chunk.text,voice,"",true);
    if(result.ids.empty()||result.ids.size()>700)throw std::runtime_error("Unsupported phoneme count in chunk");
    phonemes.push_back(result.ids);
  }
  WavWriter wav(output,config.rate);
  auto cpu=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
  const char* input_names[]={"input","input_lengths","scales"};
  const char* output_names[]={"output"};
  std::array<float,3> scales=config.scales;
  const std::array<int64_t,1> shape1{1},shape3{3};
  std::size_t total_ids=0;
  for(std::size_t i=0;i<phonemes.size();++i){
    auto& ids=phonemes[i];total_ids+=ids.size();
    int64_t count=static_cast<int64_t>(ids.size());
    const std::array<int64_t,2> id_shape{1,count};
    std::vector<Ort::Value> inputs;
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(cpu,ids.data(),ids.size(),id_shape.data(),2));
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(cpu,&count,1,shape1.data(),1));
    inputs.push_back(Ort::Value::CreateTensor<float>(cpu,scales.data(),scales.size(),shape3.data(),1));
    auto outputs=session->Run(Ort::RunOptions{nullptr},input_names,inputs.data(),inputs.size(),output_names,1);
    if(outputs.size()!=1||!outputs[0].IsTensor())throw std::runtime_error("Invalid Piper output tensor");
    const auto info=outputs[0].GetTensorTypeAndShapeInfo();
    if(info.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)throw std::runtime_error("Expected float audio output");
    const auto samples=info.GetElementCount();
    if(samples<1||samples>static_cast<std::size_t>(config.rate)*1800)throw std::runtime_error("Audio output exceeds resources");
    wav.append(outputs[0].GetTensorData<float>(),samples);
    if(chunks[i].pause_after_ms)wav.silence(chunks[i].pause_after_ms);
  }
  wav.finish();
  std::cout<<"NEO_LITE_INFER_OK chunks="<<chunks.size()<<" ids="<<total_ids
           <<" seconds="<<static_cast<double>(wav.samples)/config.rate
           <<" sample_rate="<<config.rate<<" output="<<output.string()<<"\n";
  return 0;
}
 }
// Public entry point reused inside the optional complete Neo TTS executable.
int neo_lite_cli(const std::vector<std::string>& args){return neo_lite_cli_impl(args);}
#ifndef NEO_LITE_EMBED_ONLY
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){
  try{data_path();std::vector<std::string> args;for(int i=0;i<argc;++i)args.push_back(utf8(argv[i]));return neo_lite_cli(args);}
  catch(const std::exception& e){std::cerr<<"Open Neo Lite: "<<e.what()<<"\n";return 1;}
}
#else
int main(int argc,char** argv){
  try{return neo_lite_cli({argv,argv+argc});}
  catch(const std::exception& e){std::cerr<<"Open Neo Lite: "<<e.what()<<"\n";return 1;}
}
#endif

#endif // NEO_LITE_EMBED_ONLY
