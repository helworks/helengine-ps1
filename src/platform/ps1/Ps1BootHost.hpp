#pragma once

#include "psyqo/application.hh"

namespace helengine::ps1 {

/**
 * Native PS1 application adapter.  PSYQo owns the GPU and frame lifecycle;
 * this host owns only the persistent root-scene relationship.
 */
class Ps1BootHost final : public psyqo::Application {
  private:
    void prepare() override;
    void start() override;
    void frame() override;
    void createScene() override;
};

}  // namespace helengine::ps1
