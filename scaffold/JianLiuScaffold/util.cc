#include "util.h"
#include "log.h"
#include <iostream>
#include <random>
#include <iomanip>
#include <atomic>
#include <cctype>
#include <algorithm>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace JianLiuUtil {

// 将 Json::Value 序列化为字符串
std::optional<std::string> JSON::serialize(const Json::Value& val, bool styled) {
    Json::StreamWriterBuilder builder;
    if (!styled) {
        builder["indentation"] = "";
    }
    std::unique_ptr<Json::StreamWriter> swp(builder.newStreamWriter());
    std::stringstream ss;
    int ret = swp->write(val, &ss);
    if (ret != 0) {
        ERR("序列化失败！");
        return std::optional<std::string>();
    }
    return ss.str();
}
// 将字符串解析为 Json::Value
std::optional<Json::Value> JSON::unserialize(const std::string& input) {
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value val;
    std::string errs;
    bool ret = reader->parse(input.c_str(), input.c_str() + input.size(), &val, &errs);
    if (ret == false) {
        ERR("{} 反序列化失败: {}", input, errs);
        return std::optional<Json::Value>();
    }
    return val;
}

// 读取整个文件内容到字符串
bool FUTIL::read(const std::string &filename, std::string &body) {
    std::ifstream ifs;
    ifs.open(filename, std::ios::in | std::ios::binary);
    if (ifs.is_open() == false) {
        ERR("打开文件失败: {}", filename);
        return false;
    }
    ifs.seekg(0, std::ios::end);
    size_t flen = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    body.resize(flen);
    ifs.read(&body[0], flen);
    if (ifs.good() == false) {
        ERR("读取文件数据失败: {}", filename);
        ifs.close();
        return false;
    }
    ifs.close();
    return true;
}
// 将字符串内容覆盖写入文件
bool FUTIL::write(const std::string &filename, const std::string &body) {
    std::ofstream ofs;
    ofs.open(filename, std::ios::out | std::ios::binary | std::ios::trunc);
    if (ofs.is_open() == false) {
        ERR("打开文件失败: {}", filename);
        return false;
    }
    ofs.write(body.c_str(), body.size());
    if (ofs.good() == false) {
        ERR("写入文件数据失败: {}", filename);
        ofs.close();
        return false;
    }
    ofs.close();
    return true;
}

// 按分隔符切分字符串并返回片段数量
size_t STR::split(const std::string &src, const std::string &sep, std::vector<std::string> &dst) {
    size_t pos, idx = 0;
    while(1) {
        pos = src.find(sep, idx);
        if (pos == std::string::npos) {
            break;
        }
        // 1,2,3,,5
        if (pos == idx) {
            idx = pos + sep.size();
            continue;
        }
        dst.push_back(src.substr(idx, pos - idx));
        idx = pos + sep.size();
    }
    // 1,2,3,4
    if (idx < src.size()) {
        dst.push_back(src.substr(idx));
    }
    return dst.size();
}

// 生成指定长度和字符类型的随机字符串
std::string Random::code(size_t len, UuidType utype) {
    static const char* digit_arr = "1234567890";
    static const char* char_arr = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static const char* mix_arr = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890";
    static std::atomic<unsigned char> number_base(0);
    static const size_t MIN_LEN = 6;
    // 生成随机字符串：不断产生随机数(0~数组长度), 从指定的字符串中提取出字符，组合成一个随机字符串
    // 1. 根据utype,决定从哪个字符串中提取字符
    std::string array;
    if (utype == UuidType::DIGIT) { array = digit_arr; }
    else if (utype == UuidType::CHAR){ array = char_arr; }
    else { array = mix_arr; }

    std::stringstream result;
    // 2. 循环len，获取随机数字，从字符串中提取字符 -- 提取出len-4个字符
    //  1. 生成一个机器随机数，作为伪随机数种子
    std::random_device rd;
    //  2. 根据种子，构造伪随机数引擎      
    std::mt19937 generator(rd());
    for (int i = 0; i < len; i++) {
        //  2. 生成伪随机数，并对字符串长度进行取模
        int idx = generator() % array.size();
        result << array[idx];
    }
    // 要产生的字符串长度甚至不足要求的最小长度，则直接返回随机字符串，不再做编号处理
    if (len <= MIN_LEN) {
        return result.str();
    }
    // 3. 设置一个4个字符的编号字符串进行连接
    int num = number_base.fetch_add(1);
    result << std::setw(4) << std::setfill('0') << num;
    // 4. 最终组合成为一个随机字符串，进行返回即可
    return result.str();
}
// 生成指定区间内的随机整数
size_t Random::number(size_t min, size_t max) {
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

namespace {
    // 把字节数组编码为小写十六进制字符串
    std::string hexEncode(const unsigned char *data, size_t len) {
        static const char *digits = "0123456789abcdef";
        std::string out;
        out.reserve(len * 2);
        for (size_t i = 0; i < len; i++) {
            out.push_back(digits[data[i] >> 4]);
            out.push_back(digits[data[i] & 0x0F]);
        }
        return out;
    }

    // 把十六进制字符串解码为字节数组
    bool hexDecode(const std::string &hex, std::vector<unsigned char> &out) {
        if (hex.size() % 2 != 0) {
            return false;
        }
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < hex.size(); i += 2) {
            int hi = nibble(hex[i]);
            int lo = nibble(hex[i + 1]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            out.push_back((unsigned char)((hi << 4) | lo));
        }
        return true;
    }

    // 计算 SHA-256，取前 bytes 字节的十六进制
    std::string sha256Hex(const std::string &data, size_t bytes) {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int len = 0;
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        if (ctx == nullptr) {
            return std::string();
        }
        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
        EVP_DigestUpdate(ctx, data.data(), data.size());
        EVP_DigestFinal_ex(ctx, digest, &len);
        EVP_MD_CTX_free(ctx);
        return hexEncode(digest, std::min<size_t>(bytes, len));
    }
}

// 对明文密码加盐哈希
std::string Password::hash(const std::string &plain) {
    unsigned char salt[16];
    if (RAND_bytes(salt, sizeof(salt)) != 1) {
        // 极端情况下退化：用随机数填充（仍保证每次不同）
        std::random_device rd;
        for (size_t i = 0; i < sizeof(salt); i++) {
            salt[i] = (unsigned char)(rd() & 0xFF);
        }
    }
    std::string salt_hex = hexEncode(salt, sizeof(salt));
    // 摘要输入用「盐的十六进制 + 明文」，与 verify 保持一致
    return salt_hex + sha256Hex(salt_hex + plain, 16);
}

// 校验密码，兼容历史明文密码
bool Password::verify(const std::string &stored, const std::string &plain) {
    // 历史明文密码：长度不是 64 或不是十六进制，按明文比较
    if (stored.size() != 64) {
        return stored == plain;
    }
    std::vector<unsigned char> salt;
    if (!hexDecode(stored.substr(0, 32), salt)) {
        return stored == plain;
    }
    std::string expect = stored.substr(0, 32) + sha256Hex(stored.substr(0, 32) + plain, 16);
    return expect == stored;
}

}