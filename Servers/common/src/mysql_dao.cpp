#include "chat/mysql_dao.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <exception>
#include <functional>
#include <mutex>
#include <mysql_connection.h>
#include <mysql_driver.h>
#include <queue>
#include <thread>
#include <vector>

#include "chat/config_manager.h"
#include "chat/constants.h"
#include "chat/log.h"
#include "chat/password_crypto.h"
namespace P1 {
namespace {
constexpr uint64_t CONNECTION_IDLE_THRESHOLD = 600; // 10 minutes in seconds
constexpr int HEALTH_CHECK_TICK_SECONDS = 5;
constexpr size_t MAX_IDLE_CHECKS_PER_TICK = 2;
const std::string DEFAULT_MYSQL_HOST = "127.0.0.1";
const std::string DEFAULT_MYSQL_SCHEMA = "chat_db";
const std::string DEFAULT_MYSQL_PORT = "3306";
const uint32_t DEFAULT_MAX_POOLSIZE = 5;
constexpr int32_t DEFAULT_ACQUIRE_TIMEOUT_MS = 3000;

void LogSqlError(const char* where, const sql::SQLException& e)
{
    LOG_ERROR("[MysqlDao] %s SQLException:%s (errorCode:%d, SQLState:%s)",
        where, e.what(), e.getErrorCode(), e.getSQLState().c_str());
}
} // namespace

// MySQL 连接包装器
struct SqlConn {
    explicit SqlConn(sql::Connection* con, int64_t lasttime) : conn_(con), lastUsedTime_(lasttime) {}
    ~SqlConn()
    {
        delete conn_;
    }

    SqlConn(const SqlConn&) = delete;
    SqlConn& operator=(const SqlConn&) = delete;

    SqlConn(SqlConn&& other) noexcept : conn_(other.conn_), lastUsedTime_(other.lastUsedTime_)
    {
        other.conn_ = nullptr;
    }

    sql::Connection* conn_ = nullptr;
    int64_t lastUsedTime_ = 0;
};

class MysqlDao::SqlConnPool {
public:
    struct Options {
        uint32_t poolSize = DEFAULT_MAX_POOLSIZE;
        std::chrono::milliseconds acquireTimeout {DEFAULT_ACQUIRE_TIMEOUT_MS};
    };

    SqlConnPool(const std::string& url, const std::string& user, const std::string& pass, const std::string& schema,
        const Options& options)
        : stop_(false), url_(url), user_(user), pass_(pass), schema_(schema),
          poolSize_(std::max<uint32_t>(options.poolSize, 1)),
          acquireTimeout_(std::max(options.acquireTimeout, std::chrono::milliseconds(0)))
    {
        // 启动阶段允许部分成功：MySQL 暂时不可用时先启动服务，由后台线程持续补齐，
        // 避免进程起来后连接池永远为空、每个请求都等满超时。
        const int64_t now = NowSeconds();
        uint32_t created = 0;
        for (uint32_t i = 0; i < poolSize_; ++i) {
            auto conn = CreateConn(now);
            if (!conn) {
                continue;
            }
            conns_.push(std::move(conn));
            ++created;
        }
        if (created == 0) {
            LOG_ERROR("[MysqlDao] no MySQL connection created at startup, target:%s, will retry in background",
                url_.c_str());
        } else if (created < poolSize_) {
            LOG_WARN("[MysqlDao] MySQL pool partially initialized, target:%s, connected:%u/%u",
                url_.c_str(), created, poolSize_);
        }

        try {
            checkThread_ = std::thread([this]() {
                while (!stop_) {
                    try {
                        BackgroundHealthCheck();
                    } catch (const std::exception& e) {
                        LOG_ERROR("[MysqlDao] health check loop failed: %s", e.what());
                    } catch (...) {
                        LOG_ERROR("[MysqlDao] health check loop failed with unknown exception");
                    }
                    // 分段睡眠以便及时响应 stop_，同时让掉线连接能在数秒内补回
                    for (int i = 0; i < HEALTH_CHECK_TICK_SECONDS && !stop_; ++i) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
            });
        } catch (const std::exception& e) {
            LOG_ERROR("[MysqlDao] start health check thread failed: %s", e.what());
        }
    }

    ~SqlConnPool()
    {
        Close();
    }

    std::unique_ptr<SqlConn> Acquire()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        ++waiting_;
        // 正常情况下连接毫秒级归还会立即唤醒；若连接因异常被全部丢弃或 MySQL 宕机，
        // 超时后返回 nullptr，由上层快速失败，后台线程继续补连接。
        const bool ready = cond_.wait_for(lock, acquireTimeout_, [this]() {
            return stop_ || !conns_.empty();
        });
        --waiting_;

        if (!ready || stop_ || conns_.empty()) {
            if (!stop_) {
                ++timeouts_;
                LOG_ERROR("[MysqlDao] acquire connection timeout, target:%s, available:%zu, inUse:%zu, waiting:%zu, timeouts:%llu, timeout:%lldms",
                    url_.c_str(), conns_.size(), inUse_, waiting_,
                    static_cast<unsigned long long>(timeouts_),
                    static_cast<long long>(acquireTimeout_.count()));
            }
            return nullptr;
        }

        auto conn = std::move(conns_.front());
        conns_.pop();
        ++inUse_;
        conn->lastUsedTime_ = NowSeconds();

        return conn;
    }

    void Release(std::unique_ptr<SqlConn> conn)
    {
        // 兜底：归还前确保连接处于干净状态
        //   1) 若还有未提交事务，回滚
        //   2) 恢复 autocommit
        // 任何一步失败说明连接已不可用，直接丢弃（unique_ptr 析构会 delete conn）。
        // 传入 nullptr 表示调用方在异常分支主动丢弃了连接，仍要归还 inUse 名额。
        bool reusable = false;
        if (conn && conn->conn_) {
            try {
                if (!conn->conn_->getAutoCommit()) {
                    try { conn->conn_->rollback(); } catch (...) {}
                    conn->conn_->setAutoCommit(true);
                }
                reusable = true;
            } catch (...) {
                LOG_WARN("[MysqlDao] Release: connection is broken, dropping it");
            }
        }

        std::lock_guard<std::mutex> lock(mtx_);
        if (inUse_ > 0) {
            --inUse_;
        }
        if (reusable && !stop_) {
            conn->lastUsedTime_ = NowSeconds();
            conns_.push(std::move(conn));
            cond_.notify_one();
        }
    }

    void Close()
    {
        if (stop_.exchange(true)) {
            return;
        }
        cond_.notify_all();
        if (checkThread_.joinable()) {
            checkThread_.join();
        }
        std::queue<std::unique_ptr<SqlConn>> empty;
        std::lock_guard<std::mutex> lock(mtx_);
        std::swap(conns_, empty);
    }

private:
    static int64_t NowSeconds()
    {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::seconds>(now).count();
    }

    std::unique_ptr<SqlConn> CreateConn(int64_t timestamp)
    {
        try {
            sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
            // 立即用 unique_ptr 接管，setSchema 抛异常时也能保证 conn 被释放
            std::unique_ptr<sql::Connection> rawConn(driver->connect(url_, user_, pass_));
            rawConn->setSchema(schema_);
            return std::make_unique<SqlConn>(rawConn.release(), timestamp);
        } catch (const sql::SQLException& e) {
            LogSqlError("CreateConn", e);
            return nullptr;
        } catch (const std::exception& e) {
            LOG_ERROR("[MysqlDao] create connection failed: %s", e.what());
            return nullptr;
        }
    }

    bool IsHealthy(SqlConn& conn)
    {
        try {
            // 必须用 executeQuery 并完整消费结果集；
            // 用 execute() 又不 getResultSet() 会留下未读结果，
            // 连接归还后下一条命令报 2014 Commands out of sync
            std::unique_ptr<sql::Statement> stmt(conn.conn_->createStatement());
            std::unique_ptr<sql::ResultSet> pingRes(stmt->executeQuery("SELECT 1"));
            pingRes->next();
            pingRes->close();
            return true;
        } catch (const sql::SQLException& e) {
            LogSqlError("HealthCheck", e);
            return false;
        } catch (const std::exception& e) {
            LOG_ERROR("[MysqlDao] health check failed: %s", e.what());
            return false;
        }
    }

    void BackgroundHealthCheck()
    {
        const int64_t currentTs = NowSeconds();
        std::vector<std::unique_ptr<SqlConn>> idleConns;

        {
            std::lock_guard<std::mutex> lock(mtx_);
            // 每轮最多挑少量空闲连接做健康检查，避免巡检期间把整个池搬空、业务请求无连接可用
            const size_t count = conns_.size();
            for (size_t i = 0; i < count; ++i) {
                auto conn = std::move(conns_.front());
                conns_.pop();
                if (idleConns.size() < MAX_IDLE_CHECKS_PER_TICK &&
                    currentTs - conn->lastUsedTime_ >= static_cast<int64_t>(CONNECTION_IDLE_THRESHOLD)) {
                    idleConns.push_back(std::move(conn));
                } else {
                    conns_.push(std::move(conn));
                }
            }
        }

        for (auto& conn : idleConns) {
            if (IsHealthy(*conn)) {
                conn->lastUsedTime_ = currentTs;
            } else {
                LOG_WARN("[MysqlDao] idle connection is broken, reconnecting, target:%s", url_.c_str());
                conn = CreateConn(currentTs);
            }
            if (!conn) {
                continue;
            }
            std::lock_guard<std::mutex> lock(mtx_);
            if (!stop_ && conns_.size() + inUse_ < poolSize_) {
                conns_.push(std::move(conn));
                cond_.notify_one();
            }
        }

        Replenish();
    }

    void Replenish()
    {
        // 连接可能因 SQL 异常被 DAO 主动丢弃，靠这里把池补回目标大小；
        // MySQL 恢复后无需重启服务即可自愈。
        for (;;) {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                if (stop_ || conns_.size() + inUse_ >= poolSize_) {
                    return;
                }
            }

            auto conn = CreateConn(NowSeconds());
            if (!conn) {
                return; // MySQL 仍不可用，下个巡检周期再试
            }

            {
                std::lock_guard<std::mutex> lock(mtx_);
                if (stop_) {
                    return;
                }
                if (conns_.size() + inUse_ >= poolSize_) {
                    return;
                }
                conns_.push(std::move(conn));
                cond_.notify_one();
            }
            LOG_INFO("[MysqlDao] connection replenished, target:%s, available:%zu, inUse:%zu, pool size:%u",
                url_.c_str(), conns_.size(), inUse_, poolSize_);
        }
    }

    mutable std::mutex mtx_;
    std::condition_variable cond_;
    std::queue<std::unique_ptr<SqlConn>> conns_;
    std::atomic<bool> stop_;
    std::string url_;
    std::string user_;
    std::string pass_;
    std::string schema_;
    uint32_t poolSize_;
    std::chrono::milliseconds acquireTimeout_;
    size_t inUse_ = 0;
    size_t waiting_ = 0;
    uint64_t timeouts_ = 0;
    std::thread checkThread_;
};

MysqlDao::MysqlDao()
{
    auto& cfg = ConfigManager::GetInstance();
    const std::string sectionName = "MySQL";
    auto host = cfg.GetValue<std::string>(sectionName, "Host", DEFAULT_MYSQL_HOST);
    auto port = cfg.GetValue<std::string>(sectionName, "Port", DEFAULT_MYSQL_PORT);
    auto user = cfg.GetValue<std::string>(sectionName, "User");
    auto pwd = cfg.GetValue<std::string>(sectionName, "Pwd");
    auto schema = cfg.GetValue<std::string>(sectionName, "Schema", DEFAULT_MYSQL_SCHEMA);
    auto poolMaxSize = cfg.GetValue<uint32_t>(sectionName, "PoolMaxSize", DEFAULT_MAX_POOLSIZE);
    auto acquireTimeoutMs = cfg.GetValue<int32_t>(sectionName, "AcquireTimeoutMs", DEFAULT_ACQUIRE_TIMEOUT_MS);
    if (user.empty() || pwd.empty()) {
        LOG_ERROR("[MysqlDao] create SqlConnPool failed, MySQL User or Pwd not configured");
        abort();
    }

    SqlConnPool::Options options;
    options.poolSize = poolMaxSize;
    options.acquireTimeout = std::chrono::milliseconds(std::max(acquireTimeoutMs, 1));
    pool_ = std::make_unique<SqlConnPool>(host + ":" + port, user, pwd, schema, options);
}

MysqlDao::~MysqlDao() {}

int MysqlDao::RegUserTransaction(
    const std::string& name, const std::string& email, const std::string& pwd, const std::string& icon)
{
    auto conn = pool_->Acquire();
    if (conn == nullptr) {
        return -1;
    }

    // 归还连接：确保事务已结束 + 恢复 autocommit
    Defer defer([this, &conn] {
        if (conn && conn->conn_) {
            try {
                if (!conn->conn_->getAutoCommit()) {
                    conn->conn_->rollback();
                    conn->conn_->setAutoCommit(true);
                }
            } catch (...) {}
        }
        pool_->Release(std::move(conn));
    });

    try {
        conn->conn_->setAutoCommit(false);

        // 把 id+1 的结果写入 LAST_INSERT_ID()
        {
            std::unique_ptr<sql::PreparedStatement> pstmt_upid(
                conn->conn_->prepareStatement("UPDATE user_id SET id = LAST_INSERT_ID(id + 1)"));
            pstmt_upid->executeUpdate();
        }

        // 读 LAST_INSERT_ID()：放进独立作用域，
        // 作用域结束时 ResultSet/Statement 全部析构 + 显式 close()，
        // 连接状态被完全复位，后续命令才安全
        int newId = 0;
        {
            std::unique_ptr<sql::PreparedStatement> uidPstmt(
                conn->conn_->prepareStatement("SELECT LAST_INSERT_ID() AS new_id"));
            std::unique_ptr<sql::ResultSet> uidRes(uidPstmt->executeQuery());

            if (uidRes->next()) {
                newId = uidRes->getInt("new_id");
            }
            uidRes->close(); // 关键：显式关闭，杜绝 Commands out of sync
        }

        if (newId <= 0) {
            conn->conn_->rollback();
            return -1;
        }

        // 插入用户（name / email 唯一索引兜底）
        {
            std::unique_ptr<sql::PreparedStatement> pstmt_insert(
                conn->conn_->prepareStatement(
                    "INSERT INTO user (uid, name, email, pwd, icon) VALUES (?, ?, ?, ?, ?)"));
            pstmt_insert->setInt(1, newId);
            pstmt_insert->setString(2, name);
            pstmt_insert->setString(3, email);
            // 存口令摘要而非明文（P0-2）
            pstmt_insert->setString(4, HashPassword(pwd));
            pstmt_insert->setString(5, icon);
            pstmt_insert->executeUpdate();
        }

        conn->conn_->commit();
        LOG_INFO("[MysqlDao] register user success, uid:%d", newId);
        return newId;

    } catch (sql::SQLException& e) {
        try { conn->conn_->rollback(); } catch (...) {}
        LogSqlError(__func__, e);
        conn.reset(); // 异常连接状态不可信，直接丢弃，避免污染连接池
        return -1;
    }
}

bool MysqlDao::CheckEmail(const std::string& name, const std::string& email)
{
    auto conn = pool_->Acquire();
    if (conn == nullptr) {
        return false;
    }

    Defer defer([this, &conn] { pool_->Release(std::move(conn)); });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            conn->conn_->prepareStatement("SELECT email FROM user WHERE name = ?"));
        pstmt->setString(1, name);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        bool found = false;
        std::string storeEmail;
        if (res->next()) {
            storeEmail = res->getString("email");
            found = true;
        }
        res->close(); // 关键：显式关闭

        if (found) {
            LOG_DEBUG("[MysqlDao] CheckEmail, name:%s, email:%s", name.c_str(), storeEmail.c_str());
            return email == storeEmail;
        }
        return false;

    } catch (sql::SQLException& e) {
        LogSqlError(__func__, e);
        conn.reset(); // 异常连接状态不可信，直接丢弃，避免污染连接池
        return false;
    }
}

bool MysqlDao::UpdatePassword(const std::string& email, const std::string& pwd)
{
    auto conn = pool_->Acquire();
    if (conn == nullptr) {
        return false;
    }

    Defer defer([this, &conn] { pool_->Release(std::move(conn)); });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            conn->conn_->prepareStatement("UPDATE user SET pwd = ? WHERE email = ?"));
        // 存口令摘要而非明文（P0-2）
        pstmt->setString(1, HashPassword(pwd));
        pstmt->setString(2, email);
        int updateCount = pstmt->executeUpdate();
        LOG_DEBUG("[MysqlDao] UpdatePassword, email:%s, updated rows:%d", email.c_str(), updateCount);
        return true;

    } catch (sql::SQLException& e) {
        LogSqlError(__func__, e);
        conn.reset(); // 异常连接状态不可信，直接丢弃，避免污染连接池
        return false;
    }
}

bool MysqlDao::CheckPassword(const std::string& name, const std::string& pwd, UserInfo& userInfo)
{
    auto conn = pool_->Acquire();
    if (conn == nullptr) {
        return false;
    }

    Defer defer([this, &conn] { pool_->Release(std::move(conn)); });

    try {
        // 取出口令摘要后在进程内做恒定时间比较（P0-2）：
        // 不再用 SQL 的 pwd = ? 比较——DB 的字符串比较非恒定时间，且不在我方代码内
        std::unique_ptr<sql::PreparedStatement> pstmt(
            conn->conn_->prepareStatement("SELECT uid, email, pwd FROM user WHERE name = ?"));
        pstmt->setString(1, name);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        std::string storedHash;
        std::string email;
        int uid = 0;
        bool found = false;
        if (res->next()) {
            storedHash = res->getString("pwd");
            email = res->getString("email");
            uid = res->getInt("uid");
            found = true;
        }
        res->close(); // 关键：显式关闭

        // 用户不存在与口令错误一律返回 false，避免用户名枚举差异
        if (!found || !ConstantTimeEquals(storedHash, HashPassword(pwd))) {
            return false;
        }
        userInfo.name = name;
        userInfo.email = email;
        userInfo.uid = uid;
        return true;

    } catch (sql::SQLException& e) {
        LogSqlError(__func__, e);
        conn.reset(); // 异常连接状态不可信，直接丢弃，避免污染连接池
        return false;
    }
}

std::shared_ptr<UserInfo> MysqlDao::GetUserByUid(uint32_t uid)
{
    auto conn = pool_->Acquire();
    if (conn == nullptr) {
        return nullptr;
    }

    Defer defer([this, &conn] { pool_->Release(std::move(conn)); });

    try {
        // 列收窄：pwd 列不再进入结果集（回包/缓存均不需要，减少 IO 且避免敏感数据驻留内存）
        // 注意：desc 是 MySQL 保留字（ORDER BY DESC），必须反引号转义，否则整条 SQL 语法报错
        std::unique_ptr<sql::PreparedStatement> pstmt(
            conn->conn_->prepareStatement(
                "SELECT uid, sex, name, email, nick, `desc`, icon FROM user WHERE uid = ?"));
        pstmt->setInt(1, uid);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        std::shared_ptr<UserInfo> userInfo;
        if (res->next()) {
            userInfo = std::make_shared<UserInfo>();
            userInfo->uid = res->getInt("uid");
            userInfo->sex = res->getInt("sex");
            userInfo->name = res->getString("name");
            userInfo->email = res->getString("email");
            userInfo->nick = res->getString("nick");
            userInfo->desc = res->getString("desc");
            userInfo->icon = res->getString("icon");
        }
        res->close(); // 关键：显式关闭

        return userInfo;

    } catch (sql::SQLException& e) {
        LogSqlError(__func__, e);
        conn.reset(); // 异常连接状态不可信，直接丢弃，避免污染连接池
        return nullptr;
    }
}

std::shared_ptr<UserInfo> MysqlDao::GetUserByName(const std::string& name)
{
    auto conn = pool_->Acquire();
    if (conn == nullptr) {
        return nullptr;
    }

    Defer defer([this, &conn] { pool_->Release(std::move(conn)); });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            conn->conn_->prepareStatement("SELECT * FROM user WHERE name = ?"));
        pstmt->setString(1, name);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        std::shared_ptr<UserInfo> userInfo;
        if (res->next()) {
            userInfo = std::make_shared<UserInfo>();
            userInfo->uid = res->getInt("uid");
            userInfo->sex = res->getInt("sex");
            userInfo->name = res->getString("name");
            userInfo->email = res->getString("email");
            userInfo->nick = res->getString("nick");
            userInfo->desc = res->getString("desc");
            userInfo->icon = res->getString("icon");
        }
        res->close(); // 关键：显式关闭

        return userInfo;

    } catch (sql::SQLException& e) {
        LogSqlError(__func__, e);
        conn.reset(); // 异常连接状态不可信，直接丢弃，避免污染连接池
        return nullptr;
    }
}

bool MysqlDao::AddFriendApply(int32_t from, int32_t to)
{
    auto conn = pool_->Acquire();
    if (conn == nullptr) {
        return false;
    }

    Defer defer([this, &conn] { pool_->Release(std::move(conn)); });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->conn_->prepareStatement(
            "INSERT INTO friend_apply (from_uid, to_uid) values (?,?) "
            "ON DUPLICATE KEY UPDATE status = 0 "));
        pstmt->setInt(1, from);
        pstmt->setInt(2, to);
        int rowAffected = pstmt->executeUpdate();
        if (rowAffected < 0) {
            return false;
        }
        return true;
    }
    catch (sql::SQLException& e) {
        LogSqlError(__func__, e);
        conn.reset(); // 异常连接状态不可信，直接丢弃，避免污染连接池
        return false;
    }
}

bool MysqlDao::AuthFriendApply(int32_t from, int32_t to, int32_t status)
{
    auto conn = pool_->Acquire();
    if (conn == nullptr) {
        return false;
    }

    Defer defer([this, &conn] { pool_->Release(std::move(conn)); });

    try {
        // 注意：from_uid 是申请者，to_uid 是被申请者
        // 双向记录都要置为同一处理结果，避免反向记录残留 status=0
        // status: 1=同意 2=拒绝
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->conn_->prepareStatement(
            "UPDATE friend_apply SET status = ? "
            "WHERE (from_uid = ? AND to_uid = ?) "
            "   OR (from_uid = ? AND to_uid = ?)"));

        pstmt->setInt(1, status);
        pstmt->setInt(2, from);
        pstmt->setInt(3, to);
        pstmt->setInt(4, to);
        pstmt->setInt(5, from);
        int rowAffected = pstmt->executeUpdate();

        // rowAffected == 0 表示没有找到对应的申请记录
        // 可能申请不存在、已被处理，或 from/to 不匹配
        if (rowAffected == 0) {
            LOG_WARN("[MysqlDao] AuthFriendApply no matching apply record, from:%d, to:%d, status:%d",
                from, to, status);
            return false;
        }

        return true;
    }
    catch (sql::SQLException& e) {
        LogSqlError(__func__, e);
        conn.reset(); // 异常连接状态不可信，直接丢弃，避免污染连接池
        return false;
    }
}

bool MysqlDao::AddFriend(int32_t from, int32_t to, const std::string& back_name)
{
    // 防止自己加自己
    if (from == to) {
        LOG_WARN("[MysqlDao] AddFriend rejected, from and to are the same, uid:%d", from);
        return false;
    }

    auto conn = pool_->Acquire();
    if (conn == nullptr) {
        return false;
    }

    // Defer 只负责释放连接；事务状态由函数内部显式管理
    Defer defer([this, &conn] {
        if (conn && conn->conn_) {
            try {
                // 如果事务未提交，回滚并恢复自动提交
                if (!conn->conn_->getAutoCommit()) {
                    conn->conn_->rollback();
                    conn->conn_->setAutoCommit(true);
                }
            } catch (...) {
                // 忽略回滚时的异常
            }
        }
        pool_->Release(std::move(conn));
    });

    try {
        // 开始事务
        conn->conn_->setAutoCommit(false);

        // 插入 from -> to 的好友关系，备注为 back_name
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->conn_->prepareStatement(
            "INSERT IGNORE INTO friend(self_id, friend_id, back) VALUES (?, ?, ?)"));
        pstmt->setInt(1, from);
        pstmt->setInt(2, to);
        pstmt->setString(3, back_name);

        int rowAffected = pstmt->executeUpdate();
        // INSERT IGNORE 返回 0 表示记录已存在，属于正常情况，不视为错误
        if (rowAffected == 0) {
            LOG_DEBUG("[MysqlDao] AddFriend relation already exists, from:%d, to:%d", from, to);
        }

        // 插入反向关系 to -> from，备注为空
        std::unique_ptr<sql::PreparedStatement> pstmt2(conn->conn_->prepareStatement(
            "INSERT IGNORE INTO friend(self_id, friend_id, back) VALUES (?, ?, ?)"));
        pstmt2->setInt(1, to);
        pstmt2->setInt(2, from);
        pstmt2->setString(3, "");

        int rowAffected2 = pstmt2->executeUpdate();
        if (rowAffected2 == 0) {
            LOG_DEBUG("[MysqlDao] AddFriend reverse relation already exists, from:%d, to:%d", to, from);
        }

        // 提交事务
        conn->conn_->commit();
        // 恢复自动提交，避免 Defer 中再次回滚
        conn->conn_->setAutoCommit(true);

        LOG_INFO("[MysqlDao] AddFriend success, from:%d, to:%d", from, to);
        return true;
    }
    catch (sql::SQLException& e) {
        // 回滚事务
        try {
            if (conn && conn->conn_) {
                conn->conn_->rollback();
                conn->conn_->setAutoCommit(true);
            }
        } catch (...) {
            // 忽略回滚异常
        }

        LogSqlError(__func__, e);
        conn.reset(); // 异常连接状态不可信，直接丢弃，避免污染连接池
        return false;
    }
}

bool MysqlDao::GetApplyList(int32_t uid, std::vector<std::shared_ptr<ApplyInfo>>& list,
                            int32_t begin, int32_t limit)
{
    // limit 校验
    if (limit <= 0) {
        return false;  // 或 false，看你的语义约定
    }
    // 可选：限制最大 limit，避免一次拉太多
    constexpr int64_t kMaxLimit = 1000;
    if (limit > kMaxLimit) {
        limit = kMaxLimit;
    }

    auto conn = pool_->Acquire();
    if (conn == nullptr) {
        return false;
    }

    Defer defer([this, &conn] { pool_->Release(std::move(conn)); });

    try {
        // 使用反引号避免关键字冲突；id 用 BIGINT 时 setInt64
        // last_id 语义：上一页返回的最大 id，首次传 0
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->conn_->prepareStatement(
            "SELECT fa.from_uid, fa.status, u.name, u.nick, u.sex "
            "FROM `friend_apply` AS fa "
            "JOIN `user` AS u ON fa.from_uid = u.uid "
            "WHERE fa.to_uid = ? AND fa.id > ? "
            "ORDER BY fa.id ASC "
            "LIMIT ?"));

        pstmt->setInt(1, uid);
        pstmt->setInt64(2, begin);
        pstmt->setInt64(3, limit);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while (res->next()) {
            // 用 from_uid 避免遮蔽函数参数 uid
            int32_t from_uid = res->getInt("from_uid");
            int32_t status   = res->getInt("status");
            int32_t sex      = res->getInt("sex");

            // 处理 NULL：getString 遇到 NULL 可能抛异常，用 isNull 判断
            std::string name;
            if (!res->isNull("name")) {
                name = res->getString("name");
            }
            std::string nick;
            if (!res->isNull("nick")) {
                nick = res->getString("nick");
            }

            auto info = std::make_shared<ApplyInfo>(
                from_uid, name, "", "", nick, sex, status);
            list.push_back(std::move(info));
        }

        // 不再显式 close()，让 ResultSet 析构自动关闭，避免 close 抛异常
        // 导致已经读到的数据被丢弃
        return true;
    }
    catch (sql::SQLException& e) {
        LogSqlError(__func__, e);
        conn.reset(); // 异常连接状态不可信，直接丢弃，避免污染连接池
        return false;
    }
}

bool MysqlDao::GetFriendList(int32_t uid, std::vector<std::shared_ptr<UserInfo>>& list)
{
    auto conn = pool_->Acquire();
    if (conn == nullptr) {
        return false;
    }

    Defer defer([this, &conn] { pool_->Release(std::move(conn)); });

    try {
        // 一次 JOIN 查出好友 + 用户信息，避免 N+1 查询
        // 也避免在持有连接时再次去连接池 Acquire 造成死锁
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->conn_->prepareStatement(
            "SELECT f.friend_id, f.back, u.uid, u.name, u.nick, u.sex "
            "FROM `friend` AS f "
            "JOIN `user` AS u ON f.friend_id = u.uid "
            "WHERE f.self_id = ? "
            "ORDER BY f.friend_id ASC"));

        pstmt->setInt(1, uid);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while (res->next()) {
            int32_t friend_id = res->getInt("friend_id");
            int32_t sex       = res->getInt("sex");

            std::string name;
            if (!res->isNull("name")) {
                name = res->getString("name");
            }
            std::string nick;
            if (!res->isNull("nick")) {
                nick = res->getString("nick");
            }
            std::string back;
            if (!res->isNull("back")) {
                back = res->getString("back");
            }

            // 每次 new 一个新的 UserInfo，绝不修改缓存中的 shared_ptr
            auto info = std::make_shared<UserInfo>();
            info->uid  = friend_id;
            info->name = name;
            info->nick = nick;
            info->sex  = sex;

            // back 为空时用 name 兜底，而不是无条件覆盖
            info->back = back.empty() ? name : back;

            list.push_back(std::move(info));
        }

        return true;
    }
    catch (sql::SQLException& e) {
        LogSqlError(__func__, e);
        conn.reset(); // 异常连接状态不可信，直接丢弃，避免污染连接池
        return false;
    }
}
} // namespace P1
