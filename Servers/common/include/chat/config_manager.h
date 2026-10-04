#ifndef _P1_UTILS_CONFIG_MANAGE_H
#define _P1_UTILS_CONFIG_MANAGE_H

#include <boost/lexical_cast.hpp>
#include <mutex>
#include <unordered_map>

#include "chat/log.h"
#include "chat/noncopyable.h"
namespace P1 {
class ConfigManager

{
    using Section = std::unordered_map<std::string, std::string>;
    using IniMap = std::unordered_map<std::string, Section>;

public:
    static ConfigManager& GetInstance()
    {
        static ConfigManager instance_;
        return instance_;
    }
    DISALLOW_COPY_MOVE(ConfigManager);
    void Init(const std::string& configPath = "");
    ~ConfigManager() = default;
    template<typename T>
    T GetValue(const std::string& sec, const std::string& key, const T& def = T {}) const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto sit = data_.find(sec);
        if (sit == data_.end()) {
            LOG_WARN("[ConfigManager] config not found, %s:%s, use default", sec.c_str(), key.c_str());
            return def;
        }

        auto kit = sit->second.find(key);
        if (kit == sit->second.end()) {
            LOG_WARN("[ConfigManager] config not found, %s:%s, use default", sec.c_str(), key.c_str());
            return def;
        }

        try {
            return boost::lexical_cast<T>(kit->second);
        } catch (const boost::bad_lexical_cast& e) {
            LOG_ERROR("[ConfigManager] lexical_cast failed, %s:%s, value:%s, error:%s",
                sec.c_str(), key.c_str(), kit->second.c_str(), e.what());
            return def;
        }
    }

private:
    ConfigManager();
    void Reload();
    IniMap data_;
    mutable std::mutex mtx_;
    std::string configPath_;
};
} // namespace P1

#endif // _P1_UTILS_CONFIG_MANAGE_H