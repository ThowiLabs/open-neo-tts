#include "phonemes.hpp"
#include <espeak-ng/speak_lib.h>
#include <json-c/json.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace neo::phonemes {
namespace {
std::mutex espeak_mutex;
bool espeak_ready=false;
std::string espeak_root;

std::vector<std::string> utf8_points(const std::string& text) {
  std::vector<std::string> out;
  out.reserve(text.size());
  for(size_t i=0;i<text.size();) {
    unsigned char c=static_cast<unsigned char>(text[i]);
    int len=(c<0x80)?1:((c>=0xC2&&c<=0xDF)?2:((c>=0xE0&&c<=0xEF)?3:((c>=0xF0&&c<=0xF4)?4:0)));
    if(!len || i+len>text.size())throw std::runtime_error("UTF-8 inválido");
    for(int n=1;n<len;++n) {
      if((static_cast<unsigned char>(text[i+n])&0xC0)!=0x80)
        throw std::runtime_error("Secuencia UTF-8 inválida");
    }
    out.emplace_back(text.substr(i,len));
    i+=len;
    if(out.size()>50000)throw std::runtime_error("Demasiados fonemas");
  }
  return out;
}
std::string auto_data_parent() {
  namespace fs=std::filesystem;
  if(const char* env=std::getenv("NEO_ESPEAK_DATA")) {
    if(*env)return std::string(env);
  }
#ifdef __linux__
  std::error_code ec;
  auto exe=fs::read_symlink("/proc/self/exe",ec);
  if(!ec) {
    auto candidate=exe.parent_path()/"deps/sysroot/usr/lib/x86_64-linux-gnu";
    if(fs::is_directory(candidate/"espeak-ng-data"))return candidate.string();
  }
#endif
  return {};
}
void prepare_espeak(const std::string& requested) {
  namespace fs=std::filesystem;
  const std::string parent=requested.empty()?auto_data_parent():requested;
  if(!parent.empty() && !fs::is_directory(fs::path(parent)/"espeak-ng-data"))
    throw std::runtime_error("La ruta --espeak-data debe contener espeak-ng-data");
  if(!espeak_ready) {
    const int sample_rate=espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS,0,
                 parent.empty()?nullptr:parent.c_str(),espeakINITIALIZE_DONT_EXIT);
    if(sample_rate<=0)throw std::runtime_error("No se pudo inicializar eSpeak-NG; comprueba sus datos de idioma");
    espeak_root=parent;
    espeak_ready=true;
  } else if(!requested.empty() && parent!=espeak_root) {
    throw std::runtime_error("eSpeak-NG ya está inicializado con otro directorio de datos");
  }
}
std::string json_string(json_object* obj,const char* key,const std::string& fallback) {
  json_object* value=nullptr;
  if(!json_object_object_get_ex(obj,key,&value))return fallback;
  if(!value||json_object_get_type(value)!=json_type_string)
    throw std::runtime_error(std::string("Campo JSON inválido: ")+key);
  return json_object_get_string(value);
}
std::vector<std::string> json_glyph_array(json_object* node) {
  if(json_object_get_type(node)!=json_type_array)
    throw std::runtime_error("phoneme_map debe contener arrays");
  std::vector<std::string> v;
  for(size_t i=0;i<json_object_array_length(node);++i) {
    auto* child=json_object_array_get_idx(node,i);
    if(!child||json_object_get_type(child)!=json_type_string)
      throw std::runtime_error("phoneme_map debe contener strings");
    v.emplace_back(json_object_get_string(child));
  }
  return v;
}
void append_ids(Result& out,const VoiceConfig& voice,const std::string& symbol,bool strict) {
  auto it=voice.ids.find(symbol);
  if(it==voice.ids.end()) {
    ++out.unknown;
    if(strict)throw std::runtime_error("Un símbolo IPA no pertenece al vocabulario de esta voz");
    return;
  }
  out.ids.insert(out.ids.end(),it->second.begin(),it->second.end());
}
} // namespace

VoiceConfig read_voice_config_json(const std::string& raw) {
  if(raw.empty()||raw.size()>1024*1024)
    throw std::runtime_error("Configuración de voz inexistente o mayor a 1 MiB");
  json_tokener* tok=json_tokener_new();
  if(!tok)throw std::bad_alloc();
  json_object* root=json_tokener_parse_ex(tok,raw.data(),static_cast<int>(raw.size()));
  const auto parse_error=json_tokener_get_error(tok);
  json_tokener_free(tok);
  if(parse_error!=json_tokener_success||!root) {
    if(root)json_object_put(root);
    throw std::runtime_error("JSON de configuración de voz inválido");
  }
  VoiceConfig voice;
  try {
    json_object *espeak=nullptr,*mapping=nullptr,*phoneme_map=nullptr;
    if(json_object_object_get_ex(root,"espeak",&espeak)) {
      if(!espeak||json_object_get_type(espeak)!=json_type_object)
        throw std::runtime_error("espeak debe ser un objeto JSON");
      voice.language=json_string(espeak,"voice","es");
    }
    if(!json_object_object_get_ex(root,"phoneme_id_map",&mapping)||
       !mapping||json_object_get_type(mapping)!=json_type_object)
       throw std::runtime_error("Config requiere phoneme_id_map");
    json_object_object_foreach(mapping,key,value) {
      if(!value||json_object_get_type(value)!=json_type_array)
        throw std::runtime_error("Cada fonema debe tener una lista de IDs");
      auto symbols=utf8_points(key);
      if(symbols.size()!=1)throw std::runtime_error("La clave de fonema debe ser un carácter Unicode");
      std::vector<int64_t> ids;
      for(size_t i=0;i<json_object_array_length(value);++i) {
        json_object* id=json_object_array_get_idx(value,i);
        if(!id||json_object_get_type(id)!=json_type_int)
          throw std::runtime_error("ID de fonema debe ser un entero");
        auto number=json_object_get_int64(id);
        if(number<0||number>100000)throw std::runtime_error("ID de fonema fuera de rango");
        ids.push_back(number);
      }
      if(ids.empty())throw std::runtime_error("Mapeo fonémico vacío");
      voice.ids.emplace(key,std::move(ids));
    }
    if(json_object_object_get_ex(root,"phoneme_map",&phoneme_map)) {
      if(!phoneme_map||json_object_get_type(phoneme_map)!=json_type_object)
        throw std::runtime_error("phoneme_map inválido");
      json_object_object_foreach(phoneme_map,key,value) {
        if(utf8_points(key).size()!=1)throw std::runtime_error("phoneme_map clave inválida");
        voice.remap.emplace(key,json_glyph_array(value));
      }
    }
    if(voice.language.empty()||voice.language.size()>50||voice.ids.size()>4096)
      throw std::runtime_error("Configuración de voz excede límites permitidos");
    for(const auto* s:{"_","^","$"}) {
      if(!voice.ids.count(s))throw std::runtime_error("Falta símbolo especial en phoneme_id_map");
    }
  }catch(...) {json_object_put(root);throw;}
  json_object_put(root);
  return voice;
}

VoiceConfig read_voice_config(const std::string& path) {
  namespace fs=std::filesystem;
  std::error_code ec;
  const auto size=fs::file_size(path,ec);
  if(ec||size>1024*1024||size==0)
    throw std::runtime_error("Configuración de voz inexistente o mayor a 1 MiB");
  std::ifstream in(path,std::ios::binary);
  if(!in)throw std::runtime_error("No se pudo leer configuración de voz");
  std::string raw((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
  return read_voice_config_json(raw);
}

Result from_text(const std::string& utf8,const VoiceConfig& voice,const std::string& data_parent,bool strict) {
  if(utf8.empty()||utf8.size()>10000||utf8.find('\0')!=std::string::npos)
    throw std::invalid_argument("Texto vacío, demasiado largo o contiene NUL");
  (void)utf8_points(utf8);
  std::lock_guard<std::mutex> guard(espeak_mutex);
  prepare_espeak(data_parent);
  if(espeak_SetVoiceByName(voice.language.c_str())!=EE_OK)
    throw std::runtime_error("Idioma eSpeak-NG no disponible en los datos instalados");
  const void* cursor=utf8.c_str();
  std::string ipa;
  for(int clause=0;cursor!=nullptr&&clause<200;++clause) {
    const void* previous=cursor;
    const char* output=espeak_TextToPhonemes(&cursor,espeakCHARS_UTF8,espeakPHONEMES_IPA);
    if(!output)throw std::runtime_error("eSpeak-NG no devolvió fonemas");
    ipa+=output;
    // Stock espeak-ng does not expose Piper's patched clause terminator API.
    // Preserve an explicit ASCII terminator from each consumed text clause;
    // other Piper pronunciation differences still need parity testing.
    const char* begin=static_cast<const char*>(previous);
    const char* end=cursor?static_cast<const char*>(cursor):begin+std::strlen(begin);
    if(end>=begin) {
      std::string consumed(begin,static_cast<size_t>(end-begin));
      const auto last=consumed.find_last_not_of(" \t\r\n");
      if(last!=std::string::npos && std::string(".!?,;:").find(consumed[last])!=std::string::npos)
        ipa.push_back(consumed[last]);
    }
    if(cursor && previous==cursor)
      throw std::runtime_error("eSpeak-NG no avanzó al siguiente segmento");
  }
  if(cursor!=nullptr || ipa.size()>50000)throw std::runtime_error("Texto excede límites de fonemización");
  Result result;
  result.ipa=ipa;
  auto points=utf8_points(ipa);
  append_ids(result,voice,"^",true);
  append_ids(result,voice,"_",true);
  for(const auto& point:points) {
    auto it=voice.remap.find(point);
    const std::vector<std::string> mapped=(it==voice.remap.end())
       ?std::vector<std::string>{point}:it->second;
    for(const auto& key:mapped) {
      auto mapped_points=utf8_points(key);
      for(const auto& p:mapped_points) {
        if(voice.ids.find(p)==voice.ids.end()) {
          ++result.unknown;
          if(strict)throw std::runtime_error("Un fonema no está presente en phoneme_id_map");
          continue;
        }
        append_ids(result,voice,p,true);
        append_ids(result,voice,"_",true);
      }
    }
  }
  append_ids(result,voice,"$",true);
  if(result.ids.size()>100000)throw std::runtime_error("Demasiados IDs de fonema");
  return result;
}
} // namespace neo::phonemes
