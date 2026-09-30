//===---------------------------------------------------------------------------===//
/** @file mach_server.cpp
 *  @brief Isolated SketchyBar wire-protocol receiver for native transport tests.
 */
//===---------------------------------------------------------------------------===//

#include <bootstrap.h>
#include <mach/mach.h>

#include <algorithm>
#include <csignal>
#include <iostream>
#include <string>
#include <string_view>

namespace {

volatile std::sig_atomic_t stopped = 0;

void stop(int) {
  stopped = 1;
}

// A private test service has no launchd plist to check in to. Dynamic bootstrap
// registration is required only by this fixture, not by StatWell's client.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

[[nodiscard]] kern_return_t register_port(std::string& name, mach_port_t port) {
  return ::bootstrap_register(::bootstrap_port, name.data(), port);
}

#pragma clang diagnostic pop

class Receiver {
public:
  explicit Receiver(std::string_view name) : name_("git.felix." + std::string(name)) {}

  Receiver(const Receiver&)            = delete;
  Receiver& operator=(const Receiver&) = delete;

  ~Receiver() {
    if (registered_)
      (void)register_port(name_, MACH_PORT_NULL);
    if (port_ != MACH_PORT_NULL) {
      ::mach_port_deallocate(::mach_task_self(), port_);
      ::mach_port_mod_refs(::mach_task_self(), port_, MACH_PORT_RIGHT_RECEIVE, -1);
    }
  }

  [[nodiscard]] bool start() {
    auto result = ::mach_port_allocate(::mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port_);
    if (result == KERN_SUCCESS)
      result = ::mach_port_insert_right(::mach_task_self(), port_, port_, MACH_MSG_TYPE_MAKE_SEND);
    if (result == KERN_SUCCESS)
      result = register_port(name_, port_);
    if (result != KERN_SUCCESS) {
      std::cerr << "Mach fixture registration: " << ::mach_error_string(result) << '\n';
      return false;
    }
    registered_ = true;
    return true;
  }

  [[nodiscard]] bool receive() const {
    struct Message {
      mach_msg_header_t         header;
      mach_msg_body_t           body;
      mach_msg_ool_descriptor_t descriptor;
      char                      trailer[1'024];
    } message{};

    const auto result = ::mach_msg(&message.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(message), port_, 100, MACH_PORT_NULL);
    if (result == MACH_RCV_TIMED_OUT || result == MACH_RCV_INTERRUPTED)
      return true;
    if (result != KERN_SUCCESS) {
      std::cerr << "Mach fixture receive: " << ::mach_error_string(result) << '\n';
      return false;
    }
    if ((message.header.msgh_bits & MACH_MSGH_BITS_COMPLEX) != 0 && message.body.msgh_descriptor_count == 1
        && message.descriptor.type == MACH_MSG_OOL_DESCRIPTOR && message.descriptor.size <= 65'536) {
      const auto* data = static_cast<const char*>(message.descriptor.address);
      for (mach_msg_size_t i = 0; i < message.descriptor.size; ++i)
        std::cout << (data[i] != '\0' ? data[i] : '|');
      std::cout << '\n' << std::flush;
    }
    ::mach_msg_destroy(&message.header);
    return true;
  }

private:
  std::string name_;
  mach_port_t port_       = MACH_PORT_NULL;
  bool        registered_ = false;
};

} // namespace

int main(int argc, char** argv) {
  if (argc != 2)
    return 2;
  const std::string_view name(argv[1]);
  if (!name.starts_with("statwell_test_") || name.size() > 64 || !std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
      }))
    return 2;
  Receiver receiver(name);
  if (!receiver.start())
    return 1;
  std::signal(SIGTERM, stop);
  std::signal(SIGINT, stop);
  std::cout << "READY\n" << std::flush;
  while (stopped == 0)
    if (!receiver.receive())
      return 1;
  return 0;
}
