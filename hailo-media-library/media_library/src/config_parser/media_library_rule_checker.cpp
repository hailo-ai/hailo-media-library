#include "media_library_rule_checker.hpp"
#include "rule_checker/rule_checker_interface.h"
#include "rule_checker/eval_result.h"
#include "rule_checker/logger_interface.h"

#define MODULE_NAME LoggerType::Config
class RuleCheckerLogger : public rule_checker::ILogger
{
  public:
    RuleCheckerLogger() = default;
    virtual ~RuleCheckerLogger() = default;

    void log(rule_checker::LogLevel level, const std::string &msg, const std::string &file = "", int line = 0) override
    {
        switch (level)
        {
        case rule_checker::LogLevel::TRACE:
            LOGGER__MODULE__TRACE(MODULE_NAME, "[RuleChecker] {} (File: {}:{})", msg, file, line);
            break;
        case rule_checker::LogLevel::DEBUG:
            LOGGER__MODULE__DEBUG(MODULE_NAME, "[RuleChecker] {} (File: {}:{})", msg, file, line);
            break;
        case rule_checker::LogLevel::INFO:
            LOGGER__MODULE__INFO(MODULE_NAME, "[RuleChecker] {} (File: {}:{})", msg, file, line);
            break;
        case rule_checker::LogLevel::WARN:
            LOGGER__MODULE__WARNING(MODULE_NAME, "[RuleChecker] {} (File: {}:{})", msg, file, line);
            break;
        case rule_checker::LogLevel::ERROR:
            LOGGER__MODULE__ERROR(MODULE_NAME, "[RuleChecker] {} (File: {}:{})", msg, file, line);
            break;
        case rule_checker::LogLevel::OFF:
            // Do nothing
            break;
        }
    }

    rule_checker::LogLevel get_level() const override
    {
        auto spdlog_level = LoggerManager::get_logger(MODULE_NAME)->level();
        switch (spdlog_level)
        {
        case spdlog::level::trace:
            return rule_checker::LogLevel::TRACE;
        case spdlog::level::debug:
            return rule_checker::LogLevel::DEBUG;
        case spdlog::level::info:
            return rule_checker::LogLevel::INFO;
        case spdlog::level::warn:
            return rule_checker::LogLevel::WARN;
        case spdlog::level::err:
            return rule_checker::LogLevel::ERROR;
        default:
            LOGGER__MODULE__CRITICAL(MODULE_NAME, "Internal Error, rule checker unsupported log level from spdlog: {}",
                                     spdlog_level);
            return rule_checker::LogLevel::ERROR; // Default to Error for unsupported levels
        }
    }
};

void MediaLibraryRuleChecker::configure_logger()
{
    auto logger_wrapper = std::make_shared<RuleCheckerLogger>();
    rule_checker::set_logger(logger_wrapper);
}

media_library_return MediaLibraryRuleChecker::validate_config(const nlohmann::json &config_json)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Starting rule checker validation");
    std::string config_str = config_json.dump();
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Config JSON to be validated: {}", config_str);
    rule_checker::EvalResult result = rule_checker::validate_rules(config_str);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "======== Rule checker info messeges that didnt fail the rule ========");
    int i = 0;
    for (const auto &violation : result.info_violations)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "{}) info: Rule ID: {}, Path: {}, Message: {}", i, violation.rule_id,
                              violation.path, violation.message);
        i++;
    }
    if (!result.passed)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME,
                              "======== Rule checker validation failed with the following rule violations ========");
        i = 0;
        for (const auto &violation : result.error_violations)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "{}) Validation failed: Rule ID: {}, Path: {}, Message: {}", i,
                                  violation.rule_id, violation.path, violation.message);
            i++;
        }
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    LOGGER__MODULE__INFO(MODULE_NAME, "Rule checker validation passed successfully");
    return MEDIA_LIBRARY_SUCCESS;
}
