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

#include "mkn/mod/init.hpp"  // IWYU pragma: keep
#include "mkn/mod/def.hpp"

#include "mkn/kul/cli.hpp"
#include "mkn/kul/defs.hpp"
#include "mkn/kul/except.hpp"
#include "mkn/kul/io.hpp"
#include "mkn/kul/lang/cpp/compilers.hpp"
#include "mkn/kul/lang/def.hpp"
#include "mkn/kul/log.hpp"
#include "mkn/kul/os.hpp"
#include "mkn/kul/proc.hpp"
#include "mkn/kul/string.hpp"
#include "mkn/kul/yaml.hpp"
#include "yaml-cpp/yaml.h"

#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <map>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

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

  static std::string find_fix_include() {
    std::vector<std::string> fix{"fix_include", "fix_includes.py", "iwyu-fix-includes"};
    for (auto const& str : fix)
      if (kul::env::WHICH(str)) return str;
    KEXCEPT(Exception, "Failed to find valid fix_include binary, check PATH");
  }

  // fix_include reads iwyu output from stdin only, so a shell does the redirect
  // kul::Process re-splits its arguments, the quotes keep the shell command as one
  // only the checked files are passed, iwyu also reports on headers of other projects
  // --basedir so fix_include resolves iwyu's relative paths like the passed ones
  static void fix_includes(std::string const& args, kul::File const& iwyu_out,
                           std::vector<std::string> const& checked, std::string const& basedir) {
#if MKN_KUL_IS_WIN
    auto const quote = [](std::string const& s) { return "\"" + s + "\""; };
#else
    auto const quote = [](std::string const& s) { return "'" + s + "'"; };
#endif
    std::string cmd = find_fix_include() + " " + args + " --basedir " + quote(basedir);
    for (auto const& file : checked) cmd += " " + quote(file);
    cmd += " < " + quote(iwyu_out.full());
#if MKN_KUL_IS_WIN
    kul::Process p("cmd");
    p << "/c" << cmd;
#else
    kul::Process p("sh");
    p << "-c" << "\"" + cmd + "\"";
#endif
    KLOG(DBG) << p;
    try {
      p.start();
    } catch (kul::proc::ExitException const&) {
      // non-zero with -n when changes are needed
    }
  }

  static void VALIDATE_NODE(YAML::Node const& node) {
    using namespace kul::yaml;
    Validator({NodeValidator("inc"), NodeValidator("args"), NodeValidator("ignore"),
               NodeValidator("headers"), NodeValidator("paths"), NodeValidator("types"),
               NodeValidator("fix"), NodeValidator("system_last")})
        .validate(node);
  }

  // flags of the real compile command, without compiler prefix (e.g. ccache),
  // binary, output and input
  static std::vector<std::string> compile_args(std::string const& compileCmd) {
    auto const id = mkn::kul::lang::cpp::Compilers::INSTANCE().identify(compileCmd);
    std::vector<std::string> args;
    for (auto const& arg : id.trailing) {
      if (arg == "-o") break;
      args.push_back(arg);
    }
    return args;
  }

  static std::string read(std::string const& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }

  // fix_include puts system includes first, which can hide project headers missing their own
  // includes. In each run of #include lines (blank lines allowed in between), move the system
  // includes after the others, keeping blank line separated groups and their order.
  static std::string system_includes_last(std::string const& text) {
    std::string const eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::vector<std::string> lines;
    {
      std::stringstream ss(text);
      for (std::string line; std::getline(ss, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
      }
    }
    auto const include_kind = [](std::string const& line) -> char {  // '<', '"' or 0
      auto i = line.find_first_not_of(" \t");
      if (i == std::string::npos || line[i] != '#') return 0;
      i = line.find_first_not_of(" \t", i + 1);
      if (i == std::string::npos || line.compare(i, 7, "include") != 0) return 0;
      i = line.find_first_not_of(" \t", i + 7);
      return i != std::string::npos && (line[i] == '<' || line[i] == '"') ? line[i] : 0;
    };
    auto const blank = [](std::string const& line) {
      return line.find_first_not_of(" \t") == std::string::npos;
    };

    std::vector<std::string> res;
    for (std::size_t i = 0; i < lines.size();) {
      if (!include_kind(lines[i])) {
        res.push_back(lines[i++]);
        continue;
      }
      std::size_t end = i;  // one past the last include of this run
      for (std::size_t j = i; j < lines.size() && (include_kind(lines[j]) || blank(lines[j])); ++j)
        if (include_kind(lines[j])) end = j + 1;

      std::vector<std::vector<std::string>> project{{}}, system{{}};
      for (std::size_t j = i; j < end; ++j) {
        if (blank(lines[j])) {
          if (!project.back().empty()) project.emplace_back();
          if (!system.back().empty()) system.emplace_back();
          continue;
        }
        (include_kind(lines[j]) == '<' ? system : project).back().push_back(lines[j]);
      }
      if (project.back().empty()) project.pop_back();
      if (system.back().empty()) system.pop_back();
      if (project.empty() || system.empty()) {  // nothing to move
        for (std::size_t j = i; j < end; ++j) res.push_back(lines[j]);
      } else {
        bool first = true;
        for (auto const* groups : {&project, &system})
          for (auto const& group : *groups) {
            if (!first) res.emplace_back();
            first = false;
            res.insert(res.end(), group.begin(), group.end());
          }
      }
      i = end;
    }

    std::string out;
    for (auto const& line : res) out += line + eol;
    if (!text.empty() && text.back() != '\n' && !out.empty()) out.resize(out.size() - eol.size());
    return out;
  }

  // returns iwyu's output, empty if ignored
  std::string CHECK(std::string const& proc, std::string const& compileCmd, kul::File&& f,
                    YAML::Node const& node) {
    if (node["ignore"])
      for (auto const& ignore : mkn::kul::cli::asArgs(node["ignore"].Scalar()))
        if (f.escm().find(ignore) != std::string::npos) return "";

    kul::Process p(proc);
    for (auto const& arg : compile_args(compileCmd)) p << arg;
    if (node["args"])
      for (auto const& arg : mkn::kul::cli::asArgs(node["args"].Scalar())) p << arg;
    if (node["inc"])
      for (auto const& inc : mkn::kul::cli::asArgs(node["inc"].Scalar()))
        p << std::string{"-I"} + inc;
    if (node["headers"] && is_header(f.name()))
      for (auto const& arg : mkn::kul::cli::asArgs(node["headers"].Scalar())) p << arg;
    p << f.escm();
    KLOG(DBG) << p;
    mkn::kul::ProcessCapture pc(p);
    try {
      p.start();
    } catch (kul::proc::ExitException const&) {
      // iwyu exits non-zero whenever it has suggestions
    }
    auto const out = pc.outs() + pc.errs();
    KOUT(NON) << out;
    return out;
  }

  static std::string extension(std::string const& name) {
    auto const dot = name.rfind(".");
    return dot == std::string::npos ? "" : name.substr(dot + 1);
  }

  static bool is_header(std::string const& name) {
    static std::unordered_set<std::string> const headers{"h", "hh", "hpp", "hxx"};
    return headers.count(extension(name));
  }

  void run(mkn::mod::Context& ctx, YAML::Node const& node) KTHROW(std::exception) {
    VALIDATE_NODE(node);
    auto const state = ctx.state();
    kul::os::PushDir pushd(state.projectDir);

    std::unordered_set<std::string> types{"cpp", "cxx", "cc", "h", "hpp"};
    if (node["types"]) {
      types.clear();
      for (auto const& arg : mkn::kul::cli::asArgs(node["types"].Scalar()))
        for (auto const& type : kul::String::SPLIT(arg, ":")) types.insert(type);
    }

    std::unordered_set<std::string> files;
    for (auto const& f : state.sourceFiles)
      if (types.count(extension(f))) files.insert(f);

    if (node["paths"]) {
      for (auto const& path : kul::cli::asArgs(node["paths"].Scalar())) {
        kul::Dir d(path);
        if (!d) KEXCEPT(kul::fs::Exception, "Directory does not exist: ", d.path());
        for (auto const& file : d.files(1))
          if (types.count(extension(file.name()))) files.insert(file.real());
      }
    }

    auto proc = find_iwyu();
    std::string out;
    std::vector<std::string> checked;
    // ".cpp" so headers get the C++ compile command too
    for (auto const& file : files) {
      kul::File f(file);
      auto const o = CHECK(proc, ctx.compileCommandFor(f.escm() + ".cpp"), kul::File(file), node);
      if (o.empty()) continue;
      out += o;
      checked.push_back(f.escm());
    }

    if (node["fix"]) {
      auto const buildDir = state.extra.count("buildDir") ? state.extra.at("buildDir") : "bin";
      kul::Dir dir(buildDir);
      dir.mk();
      kul::File iwyu_out("iwyu.out", dir);
      kul::io::Writer(iwyu_out) << out;

      bool const system_last = node["system_last"] && node["system_last"].Scalar() == "true";
      std::map<std::string, std::string> before;
      if (system_last)
        for (auto const& file : checked) before[file] = read(file);
      fix_includes(node["fix"].Scalar(), iwyu_out, checked, kul::Dir(state.projectDir).real());
      for (auto const& [file, text] : before) {
        auto const fixed = read(file);
        if (fixed == text) continue;
        auto const reordered = system_includes_last(fixed);
        if (reordered == fixed) continue;
        std::ofstream(file, std::ios::binary) << reordered;
        KLOG(INF) << "System includes moved last: " << file;
      }
    }
  }
};

}  // namespace mkn::mod::cpp::iwyu

extern "C" MKN_KUL_PUBLISH mkn::mod::Module* maiken_module_construct() {
  return new mkn::mod::cpp::iwyu::Module;
}

extern "C" MKN_KUL_PUBLISH void maiken_module_destruct(mkn::mod::Module* p) { delete p; }
