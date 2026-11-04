/*
    util工具封装
        1. json序列化和反序列化
*/

#pragma once
#include <jsoncpp/json/json.h>
#include <iostream>
#include <memory>
#include <fstream>
#include <sstream>
#include <optional>
#include "log.h"

namespace JianLiuUtil {
    // JSON 序列化与反序列化工具
    class JSON {  
        public:
            // 将 Json::Value 序列化为字符串
            static std::optional<std::string> serialize(const Json::Value& val, bool styled = false);
            // 将字符串解析为 Json::Value
            static std::optional<Json::Value> unserialize(const std::string& input);
    };

    // 文件读写工具
    class FUTIL { 
        public:
            // 读取整个文件内容到字符串
            static bool read(const std::string &filename, std::string &body);
            // 将字符串内容覆盖写入文件
            static bool write(const std::string &filename, const std::string &body);
    };

    // 字符串处理工具
    class STR {
        public:
            // 按分隔符切分字符串并返回片段数量
            static size_t split(const std::string &src, const std::string &sep, std::vector<std::string> &dst);

    };
    
    // 默认随机字符串长度
    const size_t UUID_SIZE = 16;
    // 随机字符串的字符类型
    enum UuidType {
        MIX = 0,
        CHAR,
        DIGIT
    };
    // 随机字符串与随机数生成工具
    class Random {
        public:
            //16个字符长度的字母+数字随机字符串
            static std::string code(size_t len = UUID_SIZE, UuidType utype = UuidType::MIX);
            //生成一个指定区间的随机数
            static size_t number(size_t min, size_t max);
    };

    // 密码哈希：加盐 SHA-256（截断 128 位），输出 64 位十六进制（32 位盐 + 32 位摘要）
    class Password {
        public:
            // 对明文密码加盐哈希，返回盐+摘要的十六进制串
            static std::string hash(const std::string &plain);
            // 校验；兼容历史明文密码（stored 不是 64 位十六进制时按明文比较）
            static bool verify(const std::string &stored, const std::string &plain);
    };
}