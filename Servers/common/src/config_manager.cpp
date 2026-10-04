#include "chat/config_manager.h"

#include <cctype>

#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include "chat/log.h"
namespace P1 {
namespace {
constexpr char INI_PATH[] = "./config.ini";

// 敏感键（口令/token/密钥等）日志脱敏，避免凭据进入日志文件
bool IsSensitiveKey(const std::string& key)
{
    static const char* kSensitiveWords[] = {"pwd", "pass", "password", "secret", "token", "key"};
    std::string lower;
    lower.reserve(key.size());
    for (char c : key) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    for (const char* word : kSensitiveWords) {
        if (lower.find(word) != std::string::npos) {
            return true;
        }
    }
    return false;
}
} // namespace

ConfigManager::ConfigManager()
{
}

void ConfigManager::Init(const std::string& configPath) 
{
    // 核心逻辑：如果传入的路径为空，就使用默认路径
    if (!configPath.empty()) {
        configPath_ = configPath;
    } else {
        // 假设你保留了之前的默认值，比如 "./config.ini"
        configPath_ = INI_PATH; 
    }
    
    // 统一调用重载逻辑
    Reload(); 
}
void ConfigManager::Reload()
{
    // std::lock_guard<std::mutex> lk(mtx_);
    // const boost::filesystem::path path= boost::filesystem::current_path() / "config.ini";
    boost::property_tree::ptree pt;
    // boost::property_tree::ini_parser::read_ini(path.string(), pt);
    try {
        boost::property_tree::ini_parser::read_ini(configPath_, pt);

        data_.clear();
        for (const auto& sec : pt) { // sec.first = 段名
            const std::string& section = sec.first;
            for (const auto& kv : sec.second) { // kv.first = key, kv.second = value
                const std::string& value = kv.second.data();
                data_[section][kv.first] = value;
                const char* logVal = IsSensitiveKey(kv.first) ? "******" : value.c_str();
                LOG_DEBUG("[ConfigManager] load %s:%s=%s", sec.first.c_str(), kv.first.c_str(), logVal);
            }
        }
        LOG_INFO("[ConfigManager] config loaded, path:%s", configPath_.c_str());

    } catch (const boost::property_tree::ptree_error& e) {
        LOG_ERROR("[ConfigManager] parse ini failed, path:%s, error:%s", configPath_.c_str(), e.what());
    } catch (const std::exception& e) {
        LOG_ERROR("[ConfigManager] load config exception:%s", e.what());
    } catch (...) {
        LOG_ERROR("[ConfigManager] unknown error while loading config file:%s", configPath_.c_str());
    }
}
} // namespace P1
