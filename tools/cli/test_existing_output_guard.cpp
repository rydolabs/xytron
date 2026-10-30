#include "existing_output_guard.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static void must(bool ok) {
  if(!ok) throw std::runtime_error("CLI output regression assertion failed");
}
static void reject(const char* path) {
  bool rejected=false;
  try { xytron_cli::checkedExistingOutput(path); }
  catch(const std::exception&) { rejected=true; }
  must(rejected);
}
int main() {
  const char* target="guard-output-test.json";
  const char* victim="guard-victim-test.json";
  std::remove(target);
  std::remove(victim);
  { std::ofstream out(target,std::ios::binary); must(!!out); out<<"ordinary"; }
  xytron_cli::checkedExistingOutput(target);
  reject("../guard-output-test.json");
  reject("guard/../output");
  reject("nonexistent-guard-file.json");

  std::remove(target);
  { std::ofstream out(victim,std::ios::binary); must(!!out); out<<"do-not-change"; }
#ifdef _WIN32
  must(CreateHardLinkA(target,victim,nullptr)!=0);
#else
  must(link(victim,target)==0);
#endif
  reject(target);
  { std::ifstream in(victim,std::ios::binary);std::string s;in>>s;must(s=="do-not-change"); }
  must(std::remove(target)==0);

#ifndef _WIN32
  must(symlink(victim,target)==0);
  reject(target);
  must(std::remove(target)==0);
  must(symlink("nonexistent-guard-file.json",target)==0);
  reject(target);
  must(std::remove(target)==0);
#endif
  must(std::remove(victim)==0);
  std::cout<<"cli.existing_output_guard PASS\n";
  return 0;
}
