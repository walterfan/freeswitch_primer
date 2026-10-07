#pragma once

#include "check_registry.h"
#include "config.h"
#include "esl_observer.h"
#include "health.h"
#include "metrics.h"

namespace fstutorial {

class HttpServer {
public:
    HttpServer(
        Config config,
        HealthRegistry& health,
        CheckRegistry& checks,
        EventHub& events,
        MetricsRegistry& metrics);
    void run();
    void stop();

private:
    Config config_;
    HealthRegistry& health_;
    CheckRegistry& checks_;
    EventHub& events_;
    MetricsRegistry& metrics_;
    void* app_{nullptr};
};

}  // namespace fstutorial

