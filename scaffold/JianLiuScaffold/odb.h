#pragma once
#include <odb/database.hxx>
#include <odb/mysql/transaction.hxx>
#include <odb/mysql/database.hxx>

namespace JianLiuOdb {
    // MySQL 连接配置
    struct mysql_settings {
        std::string host;
        std::string user = "root";
        std::string passwd;
        std::string db;
        std::string cset = "utf8";
        unsigned int port = 3306;
        unsigned int connection_pool_size = 3;
    };

    // 数据库操作句柄工厂
    class DBFactory {
        public:
            // 创建带连接池的 MySQL 数据库句柄
            static std::shared_ptr<odb::database> mysql(const mysql_settings &settings);
    };
}