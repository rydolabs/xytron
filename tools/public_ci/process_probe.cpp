// Public CI diagnostic only. Does NOT contain unpublished Xytron implementation.
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/wait.h>
#endif
int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "--child") return 0;
  const std::string child = std::string(".xytron-build/out/Release/xytron_user_backend")
#ifdef _WIN32
    + ".exe";
  char absolute[MAX_PATH * 3] = {};
  if (!_fullpath(absolute, child.c_str(), sizeof(absolute)))
    throw std::runtime_error("relative path resolution failed");
  const std::string arguments = std::string("\"") + absolute + "\" --child";
  std::string buffer = arguments;
  buffer.push_back('\0');
  STARTUPINFOA startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  if (!CreateProcessA(absolute, &buffer[0], nullptr, nullptr, FALSE, 0,
                      nullptr, nullptr, &startup, &process))
    throw std::runtime_error("CreateProcessA failed");
  const DWORD waited = WaitForSingleObject(process.hProcess, 10000);
  DWORD exitCode = 1;
  if (waited == WAIT_OBJECT_0)
    GetExitCodeProcess(process.hProcess, &exitCode);
  else
    TerminateProcess(process.hProcess, 1);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  if (waited != WAIT_OBJECT_0 || exitCode != 0)
    throw std::runtime_error("Child exited unexpectedly");
#else
    + "";
  const std::string command = "\"" + std::string("./") + child + "\" --child";
  const int status = std::system(command.c_str());
  if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    throw std::runtime_error("POSIX executable launch failed");
#endif
  std::cout << "PUBLIC_CI_PROCESS_PATH_PROBE_PASS" << std::endl;
  return 0;
}
