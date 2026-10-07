#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "config.h"
#include "esl_observer.h"
#include "health.h"

namespace fstutorial {

class EslEventClient {
public:
    using EventHandler = std::function<void(RawEslEvent)>;
    using StateHandler = std::function<void(EslConnectionState)>;

    EslEventClient(
        EslConfig config,
        HealthRegistry& health,
        EventHandler handler = {},
        StateHandler state_handler = {});
    ~EslEventClient();

    EslEventClient(const EslEventClient&) = delete;
    EslEventClient& operator=(const EslEventClient&) = delete;

    void start();
    void stop();
    EslConnectionState state() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class EslCommandClient {
public:
    EslCommandClient(EslConfig config, HealthRegistry& health);
    ~EslCommandClient();

    EslCommandClient(const EslCommandClient&) = delete;
    EslCommandClient& operator=(const EslCommandClient&) = delete;

    std::optional<std::string> execute(const std::string& name);
    void disconnect();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace fstutorial
