//===---------------------------------------------------------------------------===//
/**
 * @file packages.cpp
 * @author LCS.Dev - StatWell
 * @brief Fixed-argv package checks with bounded parsing and process lifetime.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#include "statwell/packages.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

extern char** environ;

namespace statwell {
namespace {

constexpr std::size_t kOutputLimit = std::size_t{4} * 1'024 * 1'024;

class Fd {
public:
  explicit Fd(int value = -1) noexcept : value_(value) {}

  ~Fd() {
    if (value_ >= 0)
      ::close(value_);
  }

  Fd(const Fd&)            = delete;
  Fd& operator=(const Fd&) = delete;

  [[nodiscard]] int get() const noexcept { return value_; }

  void close() noexcept {
    if (value_ >= 0) {
      ::close(value_);
      value_ = -1;
    }
  }

private:
  int value_;
};

struct CommandResult {
  std::string output;
  int         exit_code = -1;
};

[[nodiscard]] Result<CommandResult>
run_command(std::string_view executable, PackageKind kind, std::chrono::milliseconds timeout, const std::stop_token& stop) {
  if (executable.empty() || executable.front() != '/' || executable.find('\0') != std::string_view::npos || timeout.count() < 1)
    return std::unexpected(ProbeError{ErrorCode::invalid_input});

  int descriptors[2];
  if (::pipe(descriptors) != 0)
    return std::unexpected(ProbeError{ErrorCode::system_failure, errno});
  Fd read_end(descriptors[0]);
  Fd write_end(descriptors[1]);
  if (::fcntl(read_end.get(), F_SETFL, O_NONBLOCK) != 0)
    return std::unexpected(ProbeError{ErrorCode::system_failure, errno});

  posix_spawn_file_actions_t actions;
  if (const int code = ::posix_spawn_file_actions_init(&actions); code != 0)
    return std::unexpected(ProbeError{ErrorCode::system_failure, code});
  int action_code = ::posix_spawn_file_actions_adddup2(&actions, write_end.get(), STDOUT_FILENO);
  if (action_code == 0)
    action_code = ::posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  if (action_code == 0)
    action_code = ::posix_spawn_file_actions_addclose(&actions, read_end.get());
  if (action_code == 0)
    action_code = ::posix_spawn_file_actions_addclose(&actions, write_end.get());

  posix_spawnattr_t attributes;
  int               attr_code        = ::posix_spawnattr_init(&attributes);
  const bool        attr_initialized = attr_code == 0;
  if (attr_code == 0) {
    attr_code = ::posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    if (attr_code == 0)
      attr_code = ::posix_spawnattr_setpgroup(&attributes, 0);
  }
  const std::string    path(executable);
  std::array<char*, 4> brew_args{const_cast<char*>(path.c_str()), const_cast<char*>("outdated"), const_cast<char*>("--json=v2"), nullptr};
  std::array<char*, 3> pacman_args{const_cast<char*>(path.c_str()), const_cast<char*>("--nocolor"), nullptr};
  pid_t                child      = -1;
  int                  spawn_code = action_code != 0 ? action_code : attr_code;
  if (spawn_code == 0)
    spawn_code = ::posix_spawn(
        &child, path.c_str(), &actions, &attributes, kind == PackageKind::homebrew ? brew_args.data() : pacman_args.data(), environ);
  ::posix_spawn_file_actions_destroy(&actions);
  if (attr_initialized)
    ::posix_spawnattr_destroy(&attributes);
  if (spawn_code != 0)
    return std::unexpected(ProbeError{ErrorCode::system_failure, spawn_code});

  write_end.close();
  const auto    deadline = std::chrono::steady_clock::now() + timeout;
  CommandResult result;
  bool          eof          = false;
  bool          exited       = false;
  int           status       = 0;
  bool          failed       = false;
  int           failure_code = 0;
  while (!eof || !exited) {
    if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
      failed       = true;
      failure_code = ETIMEDOUT;
      break;
    }
    struct pollfd descriptor{read_end.get(), POLLIN | POLLHUP, 0};
    if (::poll(&descriptor, 1, 25) < 0 && errno != EINTR) {
      failed       = true;
      failure_code = errno;
      break;
    }
    for (;;) {
      std::array<char, 8'192> buffer{};
      const auto              count = ::read(read_end.get(), buffer.data(), buffer.size());
      if (count > 0) {
        if (result.output.size() + static_cast<std::size_t>(count) > kOutputLimit) {
          failed       = true;
          failure_code = EOVERFLOW;
          break;
        }
        try {
          result.output.append(buffer.data(), static_cast<std::size_t>(count));
        } catch (...) {
          failed       = true;
          failure_code = ENOMEM;
          break;
        }
      } else if (count == 0) {
        eof = true;
        break;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else if (errno != EINTR) {
        failed       = true;
        failure_code = errno;
        break;
      }
    }
    if (failed)
      break;
    if (!exited) {
      const auto waited = ::waitpid(child, &status, WNOHANG);
      if (waited == child)
        exited = true;
      else if (waited < 0 && errno != EINTR) {
        failed       = true;
        failure_code = errno;
        break;
      }
    }
  }
  if (failed)
    ::kill(-child, SIGKILL);
  if (!exited) {
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
  }
  if (failed)
    return std::unexpected(ProbeError{ErrorCode::system_failure, failure_code});
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

class JsonReader {
public:
  explicit JsonReader(std::string_view input) : input_(input) {}

  [[nodiscard]] bool parse(UpdateSample& sample) {
    if (!take('{'))
      return false;
    bool formulae = false;
    bool casks    = false;
    if (take('}'))
      return false;
    do {
      std::string_view key;
      if (!read_string(key) || !take(':'))
        return false;
      if (key == "formulae" || key == "casks") {
        std::uint32_t count = 0;
        if (!count_objects(count))
          return false;
        if (key == "formulae") {
          if (formulae)
            return false;
          formulae        = true;
          sample.formulae = count;
        } else {
          if (casks)
            return false;
          casks        = true;
          sample.casks = count;
        }
      } else if (!skip_value(0))
        return false;
    } while (take(','));
    if (!take('}'))
      return false;
    whitespace();
    if (!formulae || !casks || position_ != input_.size() || sample.formulae > std::numeric_limits<std::uint32_t>::max() - sample.casks)
      return false;
    sample.total         = sample.formulae + sample.casks;
    sample.has_breakdown = true;
    return true;
  }

private:
  void whitespace() {
    while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_])))
      ++position_;
  }

  [[nodiscard]] bool take(char expected) {
    whitespace();
    if (position_ >= input_.size() || input_[position_] != expected)
      return false;
    ++position_;
    return true;
  }

  [[nodiscard]] bool read_string(std::string_view& value) {
    whitespace();
    if (position_ >= input_.size() || input_[position_] != '"')
      return false;
    ++position_;
    const auto begin = position_;
    while (position_ < input_.size()) {
      const unsigned char next = static_cast<unsigned char>(input_[position_++]);
      if (next == '"') {
        value = input_.substr(begin, position_ - begin - 1);
        return true;
      }
      if (next < 0x20)
        return false;
      if (next == '\\') {
        if (position_ >= input_.size())
          return false;
        const char escape = input_[position_++];
        if (std::string_view("\"\\/bfnrt").find(escape) != std::string_view::npos)
          continue;
        if (escape != 'u' || input_.size() - position_ < 4)
          return false;
        for (int i = 0; i < 4; ++i) {
          if (!std::isxdigit(static_cast<unsigned char>(input_[position_++])))
            return false;
        }
      }
    }
    return false;
  }

  [[nodiscard]] bool count_objects(std::uint32_t& count) {
    if (!take('['))
      return false;
    if (take(']'))
      return true;
    do {
      whitespace();
      if (position_ >= input_.size() || input_[position_] != '{' || !skip_value(0) || count == std::numeric_limits<std::uint32_t>::max())
        return false;
      ++count;
    } while (take(','));
    return take(']');
  }

  [[nodiscard]] bool skip_value(int depth) {
    if (depth > 32)
      return false;
    whitespace();
    if (position_ >= input_.size())
      return false;
    const char first = input_[position_];
    if (first == '"') {
      std::string_view ignored;
      return read_string(ignored);
    }
    if (first == '{' || first == '[') {
      ++position_;
      const char close = first == '{' ? '}' : ']';
      if (take(close))
        return true;
      do {
        if (first == '{') {
          std::string_view ignored;
          if (!read_string(ignored) || !take(':'))
            return false;
        }
        if (!skip_value(depth + 1))
          return false;
      } while (take(','));
      return take(close);
    }
    if (first == 't' || first == 'f' || first == 'n') {
      const auto literal = first == 't' ? "true" : first == 'f' ? "false" : "null";
      if (input_.substr(position_).starts_with(literal)) {
        position_ += std::string_view(literal).size();
        return true;
      }
      return false;
    }
    const auto start = position_;
    if (input_[position_] == '-')
      ++position_;
    if (position_ >= input_.size())
      return false;
    if (input_[position_] == '0')
      ++position_;
    else {
      if (input_[position_] < '1' || input_[position_] > '9')
        return false;
      while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_])))
        ++position_;
    }
    if (position_ < input_.size() && input_[position_] == '.') {
      ++position_;
      const auto fraction = position_;
      while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_])))
        ++position_;
      if (fraction == position_)
        return false;
    }
    if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-'))
        ++position_;
      const auto exponent = position_;
      while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_])))
        ++position_;
      if (exponent == position_)
        return false;
    }
    return position_ > start;
  }

  std::string_view input_;
  std::size_t      position_ = 0;
};

[[nodiscard]] Result<UpdateSample> parse_pacman(std::string_view output, int exit_code) {
  if (exit_code == 2 && output.empty())
    return UpdateSample{};
  if (exit_code != 0)
    return std::unexpected(ProbeError{ErrorCode::system_failure, exit_code});
  UpdateSample sample;
  while (!output.empty()) {
    const auto end  = output.find('\n');
    const auto line = output.substr(0, end);
    if (line.empty() || line.size() > 4'096 || line.find('\0') != std::string_view::npos
        || line.find_first_not_of(" \t\r") == std::string_view::npos || sample.total == std::numeric_limits<std::uint32_t>::max())
      return std::unexpected(ProbeError{ErrorCode::invalid_input});
    ++sample.total;
    if (end == std::string_view::npos)
      break;
    output.remove_prefix(end + 1);
  }
  return sample;
}

} // namespace

Result<UpdateSample>
sample_updates(PackageKind kind, std::string_view executable, std::chrono::milliseconds timeout, const std::stop_token& stop) {
  auto command = run_command(executable, kind, timeout, stop);
  if (!command)
    return std::unexpected(command.error());
  if (kind == PackageKind::pacman)
    return parse_pacman(command->output, command->exit_code);
  if (command->exit_code != 0)
    return std::unexpected(ProbeError{ErrorCode::system_failure, command->exit_code});
  UpdateSample sample;
  if (!JsonReader(command->output).parse(sample))
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  return sample;
}

} // namespace statwell

//===---------------------------------------------------------------------------===//
