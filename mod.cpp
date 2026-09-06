/**
Copyright (c) 2026, Philip Deegan.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
    * Neither the name of Philip Deegan nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <stdint.h>            // for uint16_t
#include <string>              // for string, allocator, operator+, basic_...
#include <unordered_set>       // for unordered_set
#include <utility>             // for pair
#include <vector>              // for vector
#include "mkn/kul/cli.hpp"     // for asArgs
#include "mkn/kul/defs.hpp"    // for MKN_KUL_PUBLISH
#include "mkn/kul/except.hpp"  // for Exception, KEXCEPT, KTHROW
#include "mkn/kul/log.hpp"     // for KLOG, KLOG_INF, KLOG_DBG
#include "mkn/kul/map.hpp"     // for Map
#include "mkn/kul/os.hpp"      // for Dir, File, WHICH, Exception, PushDir
#include "mkn/kul/proc.hpp"    // for Process, AProcess, ExitException
#include "mkn/kul/string.hpp"  // for String
#include "mkn/kul/yaml.hpp"    // for NodeValidator, Validator, yaml
#include "mkn/mod/init.hpp"    // IWYU pragma: keep
#include "yaml-cpp/node/impl.h"  // for Node::operator[], Node::Scalar
#include "yaml-cpp/node/node.h"  // for Node

namespace mkn::mod::cpp::iwyu {

class Module : public mkn::mod::Module {
 public:
  void compile(mkn::mod::Context& ctx, YAML::Node const& node) override { run(ctx, node); }

 protected:
  static std::string find_iwyu() {
    std::vector<std::string> iwyu{"iwyu", "include-what-you-use"};
    for (auto const& str : iwyu)
      if (kul::env::WHICH(str)) return str;
    KEXCEPT(Exception, "Failed to find valid iwyu binary, check PATH");
  }

  static void VALIDATE_NODE(YAML::Node const& node) {
    using namespace kul::yaml;
    Validator({NodeValidator("inc"), NodeValidator("args"), NodeValidator("ignore"),
               NodeValidator("headers"), NodeValidator("paths"), NodeValidator("types")})
        .validate(node);
  }

  void CHECK(std::string const& proc, std::string const& compileCmd, kul::File&& f,
            YAML::Node const& node) {
    if (node["ignore"])
      if (f.escm().find(node["ignore"].Scalar()) != std::string::npos) return;

    kul::Process p(proc);
    auto const firstSpace = compileCmd.find(' ');
    auto compileStr =
        firstSpace == std::string::npos ? std::string() : compileCmd.substr(firstSpace + 1);
    p << compileStr.substr(0, compileStr.rfind(" -o"));
    if (node["args"]) p << node["args"].Scalar();
    if (node["inc"])
      for (const auto& inc : mkn::kul::cli::asArgs(node["inc"].Scalar()))
        p << std::string{"-I"} + inc;
    if (node["headers"]) p << node["headers"].Scalar();
    p << f.escm();
    KLOG(DBG) << p;
    try {
      p.start();
    } catch (kul::proc::ExitException const& e) {
    }
  }
  void run(mkn::mod::Context& ctx, YAML::Node const& node) KTHROW(std::exception) {
    VALIDATE_NODE(node);
    auto const state = ctx.state();
    kul::os::PushDir pushd(state.projectDir);

    std::unordered_set<std::string> types;
    if (!node["types"]) {
      types = {"cpp", "cxx", "cc", "cc", "h", "hpp"};
    } else
      for (const auto& s : kul::String::SPLIT(node["types"].Scalar(), ":")) types.insert(s);

    std::unordered_set<std::string> files;

    for (const auto& f : state.sourceFiles) {
      auto const dot = f.rfind(".");
      if (dot == std::string::npos) continue;
      if (types.count(f.substr(dot + 1))) files.insert(f);
    }

    if (node["paths"]) {
      for (const auto& path : kul::cli::asArgs(node["paths"].Scalar())) {
        kul::Dir d(path);
        if (!d) KEXCEPT(kul::fs::Exception, "Directory does not exist: ", d.path());
        for (const auto& file : d.files(1)) {
          const std::string name = file.name();
          if (name.find(".") == std::string::npos) continue;
          const std::string type = name.substr(name.rfind(".") + 1);
          if (types.count(type)) files.insert(file.real());
        }
      }
    }

    auto proc = find_iwyu();
    for (const auto& file : files)
      CHECK(proc, ctx.compileCommandFor(kul::File(file).escm() + ".cpp"), kul::File(file), node);
  }
};

}  // namespace mkn::mod::cpp::iwyu

extern "C" MKN_KUL_PUBLISH mkn::mod::Module* maiken_module_construct() {
  return new mkn::mod::cpp::iwyu::Module;
}

extern "C" MKN_KUL_PUBLISH void maiken_module_destruct(mkn::mod::Module* p) { delete p; }
