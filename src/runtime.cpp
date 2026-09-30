//===---------------------------------------------------------------------------===//
/**
 * @file runtime.cpp
 * @author LCS.Dev - StatWell
 * @brief Cadenced system probes and atomic, owner-only snapshot publication.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#include "statwell/runtime.hpp"

#include "registry.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace statwell {
namespace {

using Clock = std::chrono::steady_clock;

volatile std::sig_atomic_t stop_requested = 0;

void request_stop(int) noexcept {
  stop_requested = 1;
}

class Fd {
public:
  explicit Fd(int value = -1) noexcept : value_(value) {}

  ~Fd() {
    if (value_ >= 0)
      ::close(value_);
  }

  Fd(const Fd&)            = delete;
  Fd& operator=(const Fd&) = delete;

  Fd(Fd&& other) noexcept : value_(other.value_) { other.value_ = -1; }

  [[nodiscard]] int get() const noexcept { return value_; }

private:
  int value_;
};

[[noreturn]] void fail(std::string_view action) {
  throw std::runtime_error(std::string(action) + ": " + std::strerror(errno));
}

[[nodiscard]] std::int64_t unix_ms() noexcept {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

[[nodiscard]] Fd open_private_dir(std::string_view path, bool create) {
  std::string name(path);
  if (name.empty() || name.front() != '/' || name.find('\0') != std::string::npos)
    throw std::runtime_error("runtime directory must be an absolute path");
  // A trailing slash would make open resolve a final symlink before O_NOFOLLOW.
  while (name.size() > 1 && name.back() == '/')
    name.pop_back();
  if (create && ::mkdir(name.c_str(), 0700) != 0 && errno != EEXIST)
    fail("create runtime directory");
  Fd dir(::open(name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (dir.get() < 0)
    fail("open runtime directory");
  struct stat metadata{};
  if (::fstat(dir.get(), &metadata) != 0)
    fail("stat runtime directory");
  if (!S_ISDIR(metadata.st_mode) || metadata.st_uid != ::geteuid() || (static_cast<unsigned int>(metadata.st_mode) & 0777U) != 0700U)
    throw std::runtime_error("runtime directory must be owned by this user with mode 0700");
  return dir;
}

void validate_request(int fd) {
  struct stat metadata{};
  if (::fstat(fd, &metadata) != 0)
    fail("stat refresh request");
  if (!S_ISREG(metadata.st_mode) || metadata.st_uid != ::geteuid() || metadata.st_nlink != 1
      || (static_cast<unsigned int>(metadata.st_mode) & 0777U) != 0600U || metadata.st_size != 0)
    throw std::runtime_error("refresh request must be an empty owner-only regular file");
}

[[nodiscard]] bool consume_refresh(int dir, std::string_view provider) {
  const std::string name = "refresh." + std::string(provider);
  Fd                request(::openat(dir, name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  if (request.get() < 0 && errno == ENOENT)
    return false;
  if (request.get() < 0)
    fail("open refresh request");
  validate_request(request.get());
  if (::unlinkat(dir, name.c_str(), 0) != 0)
    fail("consume refresh request");
  return true;
}

void schedule_package(Registration& item, Clock::time_point now) {
  if (item.source->failed()) {
    item.failures = std::min(item.failures + 1, 4U);
    item.next     = now + package_retry_delay(item.failures);
  } else {
    item.failures = 0;
    item.next     = now + item.cadence;
  }
  if (item.refresh_queued) {
    item.refresh_queued = false;
    item.next           = now;
  }
}

void write_all(int fd, std::string_view content) {
  while (!content.empty()) {
    const auto written = ::write(fd, content.data(), content.size());
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0)
      fail("write snapshot");
    content.remove_prefix(static_cast<std::size_t>(written));
  }
}

struct Publication {
  std::string_view content;
  std::string_view instance;
  std::uint64_t    sequence = 0;
};

void publish(int dir, const Publication& publication) {
  const auto& [content, instance, sequence] = publication;
  const std::string temporary               = "snapshot." + std::string(instance) + "." + std::to_string(sequence) + ".tmp";
  Fd                file(::openat(dir, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  if (file.get() < 0)
    fail("create temporary snapshot");
  try {
    if (::fchmod(file.get(), 0600) != 0)
      fail("set snapshot permissions");
    write_all(file.get(), content);
    if (::fsync(file.get()) != 0)
      fail("sync snapshot");
    if (::renameat(dir, temporary.c_str(), dir, "snapshot.json") != 0)
      fail("publish snapshot");
    if (::fsync(dir) != 0)
      fail("sync runtime directory");
  } catch (...) {
    ::unlinkat(dir, temporary.c_str(), 0);
    throw;
  }
}

[[nodiscard]] std::string snapshot(const std::vector<Registration>& registrations, std::string_view instance_id, std::uint64_t sequence) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(6);
  out << "{\"schema_version\":1,\"instance_id\":\"" << instance_id << "\",\"sequence\":" << sequence
      << ",\"captured_at_unix_ms\":" << unix_ms() << ",\"metrics\":{";
  bool first = true;
  for (const auto& item : registrations) {
    if (!first)
      out << ',';
    first = false;
    out << '"' << item.name << "\":";
    item.source->render(out, item.cadence, item.duration_us);
  }
  out << "}}\n";
  return out.str();
}

[[nodiscard]] std::string instance_id() {
  std::ostringstream out;
  out << std::hex << ::getpid() << '-' << Clock::now().time_since_epoch().count();
  return out.str();
}

class SignalHandlers {
public:
  SignalHandlers() {
    struct sigaction action{};
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    if (::sigaction(SIGINT, &action, &previous_int_) != 0)
      fail("install SIGINT handler");
    if (::sigaction(SIGTERM, &action, &previous_term_) != 0) {
      ::sigaction(SIGINT, &previous_int_, nullptr);
      fail("install SIGTERM handler");
    }
  }

  ~SignalHandlers() {
    ::sigaction(SIGINT, &previous_int_, nullptr);
    ::sigaction(SIGTERM, &previous_term_, nullptr);
  }

  SignalHandlers(const SignalHandlers&)            = delete;
  SignalHandlers& operator=(const SignalHandlers&) = delete;

private:
  struct sigaction previous_int_{};
  struct sigaction previous_term_{};
};

} // namespace

std::string default_runtime_dir() {
#ifdef __linux__
  const char* base = std::getenv("XDG_RUNTIME_DIR");
#else
  const char*             base = std::getenv("TMPDIR");
  std::array<char, 4'096> user_temp{};
  if (base == nullptr || *base == '\0') {
    const auto size = ::confstr(_CS_DARWIN_USER_TEMP_DIR, user_temp.data(), user_temp.size());
    if (size > 0 && size <= user_temp.size())
      base = user_temp.data();
  }
#endif
  if (base == nullptr || *base == '\0')
    base = "/tmp";
  std::string directory(base);
  if (directory.back() != '/')
    directory += '/';
  return directory + "statwell-" + std::to_string(::geteuid());
}

int run_daemon(const RuntimeOptions& options) {
  stop_requested = 0;
  SignalHandlers handlers;
  const auto     directory = options.runtime_dir.empty() ? default_runtime_dir() : options.runtime_dir;
  Fd             dir       = open_private_dir(directory, true);
  Fd             lock(::openat(dir.get(), "daemon.lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600));
  if (lock.get() < 0)
    fail("open daemon lock");
  struct stat lock_metadata{};
  if (::fstat(lock.get(), &lock_metadata) != 0)
    fail("stat daemon lock");
  if (!S_ISREG(lock_metadata.st_mode) || lock_metadata.st_uid != ::geteuid() || lock_metadata.st_nlink != 1
      || ::fchmod(lock.get(), 0600) != 0)
    throw std::runtime_error("daemon lock must be owned by this user and mode 0600");
  if (::flock(lock.get(), LOCK_EX | LOCK_NB) != 0)
    fail("lock daemon instance");
  auto          registrations = make_registry(options);
  const auto    id            = instance_id();
  std::uint64_t sequence      = 0;
  while (!stop_requested) {
    const auto now     = Clock::now();
    bool       changed = false;
    for (auto& item : registrations) {
      const bool package = item.name == "homebrew" || item.name == "pacman";
      bool       refresh = false;
      if (package) {
        try {
          refresh            = consume_refresh(dir.get(), item.name);
          item.refresh_error = false;
        } catch (const std::exception& error) {
          if (!item.refresh_error)
            std::cerr << "statwell: rejected refresh request: " << error.what() << '\n';
          item.refresh_error = true;
        }
      }
      if (refresh) {
        if (item.source->pending())
          item.refresh_queued = true;
        else
          item.next = now;
      }
      if (item.source->poll(item.duration_us)) {
        if (package)
          schedule_package(item, Clock::now());
        changed = true;
      }
      if (now >= item.next && !item.source->pending()) {
        const auto started = Clock::now();
        item.source->sample();
        item.duration_us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
        item.next        = Clock::now() + item.cadence;
        changed          = true;
        if (package && !item.source->pending())
          schedule_package(item, Clock::now());
      }
    }
    if (changed) {
      ++sequence;
      publish(dir.get(), {snapshot(registrations, id, sequence), id, sequence});
    }
    auto next = Clock::now() + std::chrono::milliseconds(250);
    for (const auto& item : registrations)
      if (!item.source->pending() && item.next < next)
        next = item.next;
    std::this_thread::sleep_until(next);
  }
  ::unlinkat(dir.get(), "snapshot.json", 0);
  return 0;
}

std::optional<std::string> read_snapshot(std::string_view runtime_dir) {
  const std::string directory = runtime_dir.empty() ? default_runtime_dir() : std::string(runtime_dir);
  if (::access(directory.c_str(), F_OK) != 0 && errno == ENOENT)
    return std::nullopt;
  Fd dir = open_private_dir(directory, false);
  Fd lock(::openat(dir.get(), "daemon.lock", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  if (lock.get() < 0 && errno == ENOENT)
    return std::nullopt;
  if (lock.get() < 0)
    fail("open daemon lock");
  struct stat lock_metadata{};
  if (::fstat(lock.get(), &lock_metadata) != 0)
    fail("stat daemon lock");
  if (!S_ISREG(lock_metadata.st_mode) || lock_metadata.st_uid != ::geteuid() || lock_metadata.st_nlink != 1
      || (static_cast<unsigned int>(lock_metadata.st_mode) & 0777U) != 0600U)
    throw std::runtime_error("daemon lock must be an owner-only regular file");
  if (::flock(lock.get(), LOCK_EX | LOCK_NB) == 0) {
    ::flock(lock.get(), LOCK_UN);
    return std::nullopt;
  }
  if (errno != EWOULDBLOCK)
    fail("check daemon lock");
  Fd file(::openat(dir.get(), "snapshot.json", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  if (file.get() < 0 && errno == ENOENT)
    return std::nullopt;
  if (file.get() < 0)
    fail("open snapshot");
  struct stat metadata{};
  if (::fstat(file.get(), &metadata) != 0)
    fail("stat snapshot");
  if (!S_ISREG(metadata.st_mode) || metadata.st_uid != ::geteuid() || (static_cast<unsigned int>(metadata.st_mode) & 0777U) != 0600U
      || metadata.st_size < 2 || metadata.st_size > 131'072)
    throw std::runtime_error("snapshot must be an owner-only regular file below 128 KiB");
  std::string content(static_cast<std::size_t>(metadata.st_size), '\0');
  std::size_t offset = 0;
  while (offset < content.size()) {
    const auto count = ::read(file.get(), content.data() + offset, content.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      fail("read snapshot");
    offset += static_cast<std::size_t>(count);
  }
  if (!content.starts_with("{\"schema_version\":1,"))
    throw std::runtime_error("unsupported or corrupt snapshot schema");
  return content;
}

void request_refresh(std::string_view runtime_dir, std::string_view provider) {
  if (provider != "homebrew" && provider != "pacman")
    throw std::invalid_argument("refresh requires one package provider");
  const auto content = read_snapshot(runtime_dir);
  if (!content || content->find("\"" + std::string(provider) + "\":{") == std::string::npos)
    throw std::runtime_error("refresh requires a running daemon with this provider enabled");
  Fd                dir  = open_private_dir(runtime_dir.empty() ? default_runtime_dir() : std::string(runtime_dir), false);
  const std::string name = "refresh." + std::string(provider);
  Fd                request(::openat(dir.get(), name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600));
  if (request.get() < 0 && errno == EEXIST) {
    Fd existing(::openat(dir.get(), name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (existing.get() < 0 && errno == ENOENT) {
      // The daemon consumed the coalesced request while it was being checked.
      return;
    }
    if (existing.get() < 0)
      fail("open existing refresh request");
    validate_request(existing.get());
    return;
  }
  if (request.get() < 0)
    fail("create refresh request");
  if (::fchmod(request.get(), 0600) != 0)
    fail("set refresh permissions");
  validate_request(request.get());
}

std::string one_shot_snapshot(const RuntimeOptions& options) {
  auto registrations = make_registry(options);
  for (auto& item : registrations) {
    const auto started = Clock::now();
    item.source->sample();
    item.duration_us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  for (auto& item : registrations) {
    if (item.name == "cpu" || (item.name == "network" && !options.interface_name.empty())) {
      const auto started = Clock::now();
      item.source->sample();
      item.duration_us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
    }
  }
  const auto deadline = Clock::now() + options.package_timeout + std::chrono::seconds(1);
  for (;;) {
    bool pending = false;
    for (auto& item : registrations) {
      item.source->poll(item.duration_us);
      pending = pending || item.source->pending();
    }
    if (!pending || Clock::now() >= deadline)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return snapshot(registrations, instance_id(), 1);
}

} // namespace statwell

//===---------------------------------------------------------------------------===//
