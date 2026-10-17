// Regnum — запуск дочерних процессов: CreateProcessW на Windows, fork/execv на Linux и macOS.
#include "base/process.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace rg::process {

#ifdef _WIN32
namespace {

// Кавычки по правилам разбора командной строки CRT (CommandLineToArgvW).
std::wstring quote(const std::wstring& a) {
  if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
  std::wstring r = L"\"";
  for (size_t i = 0;; i++) {
    size_t slashes = 0;
    while (i < a.size() && a[i] == L'\\') { i++; slashes++; }
    if (i == a.size()) { r.append(slashes * 2, L'\\'); break; }
    if (a[i] == L'"') r.append(slashes * 2 + 1, L'\\');
    else r.append(slashes, L'\\');
    r += a[i];
  }
  return r + L"\"";
}

}  // namespace

int run(const std::string& exe, const std::vector<std::string>& args, const std::string& outFile) {
  std::wstring wexe = utf8::toWide(exe);
  std::wstring cmd = quote(wexe);
  for (auto& a : args) cmd += L" " + quote(utf8::toWide(a));
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE out = CreateFileW(utf8::toWide(outFile).c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  if (out == INVALID_HANDLE_VALUE) return -1;
  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = out;
  si.hStdError = out;
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(wexe.c_str(), cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
  CloseHandle(out);
  if (!ok) return -1;
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = DWORD(-1);
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return int(code);
}

#else

int run(const std::string& exe, const std::vector<std::string>& args, const std::string& outFile) {
  std::vector<std::string> all;
  all.push_back(exe);
  all.insert(all.end(), args.begin(), args.end());
  std::vector<char*> argv;
  for (auto& a : all) argv.push_back(a.data());
  argv.push_back(nullptr);
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    int fd = open(outFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) _exit(127);
    dup2(fd, 1);
    dup2(fd, 2);
    close(fd);
    execv(exe.c_str(), argv.data());
    _exit(127);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0)
    if (errno != EINTR) return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

#endif

}  // namespace rg::process
