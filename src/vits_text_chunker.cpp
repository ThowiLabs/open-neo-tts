#include "vits_text_chunker.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace neo::vits {
namespace {
bool whitespace(char c) {
  return c==' '||c=='\n'||c=='\r'||c=='\t'||c=='\f'||c=='\v';
}
bool continuation(char c) {
  return (static_cast<unsigned char>(c)&0xc0)==0x80;
}
std::size_t utf8_boundary(const std::string& s,std::size_t p,std::size_t begin) {
  p=std::min(p,s.size());
  while(p>begin&&p<s.size()&&continuation(s[p]))--p;
  return p;
}
bool likely_abbreviation(const std::string& s,std::size_t dot) {
  if(s[dot]!='.')return false;
  std::size_t begin=dot;
  while(begin>0&&std::isalpha(static_cast<unsigned char>(s[begin-1])))--begin;
  std::string word=s.substr(begin,dot-begin);
  for(char& c:word)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  static const std::string known="|sr|sra|srta|dr|dra|lic|ing|arq|mtro|mtra|prof|profa|ud|uds|etc|ej|p|pag|tel|av|col|num|";
  return !word.empty()&&known.find("|"+word+"|")!=std::string::npos;
}
bool ending_mark(const std::string& s,std::size_t pos,std::size_t& end) {
  if(s[pos]=='.'&&likely_abbreviation(s,pos))return false;
  if(s[pos]=='.'||s[pos]=='!'||s[pos]=='?')end=pos+1;
  else if(pos+3<=s.size()&&s.compare(pos,3,"\xE2\x80\xA6")==0)end=pos+3;
  else return false;
  // Include closing quote(s) after punctuation, like Piper-Neo.
  while(end<s.size()&&(s[end]=='"'||s[end]=='\''||s[end]==')'||
          s[end]==']'||s[end]=='}'))++end;
  return end==s.size()||whitespace(s[end]);
}
std::size_t choose(const std::string& s,std::size_t pos,std::size_t preferred,
                   std::size_t min_clause) {
  const auto end=std::min(s.size(),pos+preferred);
  // Preserve spoken order: a comma before a period must be heard BEFORE the
  // full stop, even when the entire sentence fits in the length budget.
  // Keep Spanish questions/exclamations intact when the closing mark is near.
  for(auto i=pos+min_clause-1;i<end;++i){
    if(continuation(s[i]))continue;
    if(i+1<s.size()&&s[i]=='\n'&&s[i+1]=='\n')return i+2;
    if(i+3<s.size()&&s.compare(i,4,"\r\n\r\n")==0)return i+4;
    std::size_t mark_end=0;
    if(ending_mark(s,i,mark_end)&&mark_end<s.size()&&s.size()-mark_end>=min_clause){
      // A period before a blank line closes the paragraph as a unit.
      if(mark_end+1<s.size()&&s.compare(mark_end,2,"\n\n")==0)return mark_end+2;
      if(mark_end+3<s.size()&&s.compare(mark_end,4,"\r\n\r\n")==0)return mark_end+4;
      return mark_end;
    }
    if(s[i]!=','&&s[i]!=';'&&s[i]!=':')continue;
    if(s[i]==',' && i>0 && std::isdigit(static_cast<unsigned char>(s[i-1])) &&
       std::isdigit(static_cast<unsigned char>(s[i+1])))continue;
    if(i+1>=s.size()||!whitespace(s[i+1])||s.size()-(i+1)<min_clause)continue;
    // Avoid cutting inside an unfinished Spanish question/exclamation.
    const auto question=s.rfind("\xC2\xBF",i),answer=s.rfind('?',i);
    const auto exclamation=s.rfind("\xC2\xA1",i),closing=s.rfind('!',i);
    if((question!=std::string::npos&&question>=pos&&
         (answer==std::string::npos||answer<question))||
       (exclamation!=std::string::npos&&exclamation>=pos&&
         (closing==std::string::npos||closing<exclamation)))continue;
    return i+1;
  }
  if(end==s.size())return end;
  const auto low=pos+std::max<std::size_t>(16,preferred/3);
  const auto hard=std::min(s.size(),pos+preferred+preferred/2);
  for(const auto& pair : {std::pair<std::string,char>{"\xC2\xBF",'?'},
                          std::pair<std::string,char>{"\xC2\xA1",'!'}}) {
    const auto opening=s.find(pair.first,pos);
    if(opening!=std::string::npos&&opening<end &&
       s.find(pair.second,opening)!=std::string::npos &&
       s.find(pair.second,opening)>=end) {
      const auto closing=s.find(pair.second,end);
      if(closing!=std::string::npos&&closing+1<=hard &&
         (closing+1==s.size()||whitespace(s[closing+1])))return closing+1;
    }
  }
  std::size_t para=0,strong=0,soft=0,space=0;
  for(auto i=pos;i<end;++i){
    if(continuation(s[i]))continue;
    const auto next=i+1;
    if(((s[i]=='\n'&&(next<s.size()&&s[next]=='\n')) ||
                  (s[i]=='\r'&&(next<s.size()&&s[next]=='\n'))))
      para=(s[i]=='\r'||s[i]=='\n')?std::min(s.size(),next+1):next;
    std::size_t mark_end=0;
    if(i>=low&&ending_mark(s,i,mark_end)&&mark_end<=end)strong=mark_end;
    if(i>=low&&(s[i]==','||s[i]==';'||s[i]==':'||s[i]=='\n'))
      soft=next;
    if(i>=low&&whitespace(s[i]))space=next;
  }
  if(para)return para;
  if(strong)return strong;
  if(soft)return soft;
  if(space)return space;
  // A long unbroken word may pass the preference up to 1.5x, as in
  // Piper-Neo. A genuinely uninterrupted word is split only on UTF-8 edges.
  for(auto i=end;i<hard;++i)
    if(whitespace(s[i]))return i+1;
  const auto safe=utf8_boundary(s,hard,pos);
  if(safe<=pos)throw std::runtime_error("Cannot find safe UTF-8 boundary");
  return safe;
}
int silence_ms(const std::string& s) {
  if(s.size()>=2&&s.substr(s.size()-2)=="\n\n")return 380;
  for(std::size_t i=s.size();i>0;--i){
    const char c=s[i-1];
    if(whitespace(c)||c=='"'||c=='\''||c==')'||c==']')continue;
    if(c=='!'||c=='?'||c=='.')return 280;
    if(c==','||c==';'||c==':')return 120;
    return 55;
  }
  return 70;
}
} // namespace

std::vector<TextChunk> split_text_for_speech(
    const std::string& input,std::size_t preferred_bytes) {
  if(input.empty()||input.size()>65536||input.find('\0')!=std::string::npos)
    throw std::invalid_argument("Speech text must contain 1..65536 UTF-8 bytes without NUL");
  if(preferred_bytes<32||preferred_bytes>512)
    throw std::invalid_argument("Chunk preference outside range 32..512 bytes");
  std::vector<TextChunk> result;
  std::size_t pos=0;
  while(pos<input.size()){
    while(pos<input.size()&&whitespace(input[pos]))++pos;
    if(pos==input.size())break;
    // Bound the number of small punctuation clauses in very long documents.
    const auto min_clause=std::max<std::size_t>(5,(input.size()+1023)/1024);
    auto split=choose(input,pos,preferred_bytes,min_clause);
    if(split<=pos||split>input.size()||(split<input.size()&&continuation(input[split])))
      throw std::runtime_error("Invalid UTF-8 split boundary");
    auto segment=input.substr(pos,split-pos);
    while(!segment.empty()&&whitespace(segment.back()))segment.pop_back();
    if(!segment.empty()){
      result.push_back({std::move(segment),0});
      if(result.size()>1024)throw std::invalid_argument("Too many speech segments");
    }
    pos=split;
    if(!result.empty()) {
      const bool paragraph=split>=2&&
              input[split-1]=='\n'&&input[split-2]=='\n';
      result.back().pause_after_ms=paragraph?380:silence_ms(result.back().text);
    }
  }
  if(result.empty())throw std::invalid_argument("No spoken content");
  result.back().pause_after_ms=0;
  return result;
}
} // namespace neo::vits
