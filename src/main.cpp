#include "platform/ps1/Ps1BootHost.hpp"

int main() {
    static helengine::ps1::Ps1BootHost host;
    return host.run();
}
