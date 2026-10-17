#pragma once
#include <functional>
#include <string>
namespace neo::web {
using Synthesize = std::function<void(const std::string&,const std::string&)>;
struct Options { std::string user,password; bool shared=false; };
int serve(int port,const std::string& engine,const Synthesize& synth,const Options& options={});
}
