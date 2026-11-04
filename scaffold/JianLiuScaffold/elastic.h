#pragma once
#include "util.h"
#include <cpr/response.h>
#include <elasticlient/client.h>

namespace JianLiuIcs {
    // JSON 节点基类：持有 key 和自身字段值，是 Object/Array 的公共父类
    class Base {
        public:
            using ptr = std::shared_ptr<Base>;
            // 构造节点并记录其在父对象中的 key
            Base(const std::string &key);
            //向val对象中添加数据：基础类型数据，数组类型数据
            template<typename T>
            void add(const std::string& key, const T &val) {
                _val[key] = val;
            }
            // 向数组类字段追加一个值
            template<typename T>
            void append(const std::string& key, const T &val) {
                _val[key].append(val);
            }
            // 返回节点在父对象中的键名
            const std::string& key() const;
            // 返回节点自身的 JSON 值（不含子节点）
            virtual const Json::Value val() const;
            // 将当前节点序列化为带 key 的 JSON 字符串
            virtual std::string to_string() const;
        protected:
            std::string _key;
            Json::Value _val;
    };
    class Array;
    // 对象节点：可包含具名子节点，对应 JSON object
    class Object: public Base {
        public:
            using ptr = std::shared_ptr<Object>;
            // 构造指定 key 的对象节点
            Object(const std::string &key);
            // 按 key 查找并转换为指定类型的子节点，不存在返回空
            template<typename R>
            std::shared_ptr<R> getElement(const std::string &key) {
                auto it = _subs.find(key);
                if (it == _subs.end()) {
                    return nullptr;
                }
                return std::dynamic_pointer_cast<R>(it->second);
            }
            // 获取或创建指定 key 的对象子节点
            std::shared_ptr<Object> newObject(const std::string &key);
            // 获取或创建指定 key 的数组子节点
            std::shared_ptr<Array> newArray(const std::string &key);
            // 把子节点加入当前对象
            void addElement(const Base::ptr &sub);
            // 递归汇总自身字段与所有子节点的 JSON 值
            virtual const Json::Value val() const;
            // 将对象及其子节点序列化为 JSON 字符串
            virtual std::string to_string() const;
        private:
            std::unordered_map<std::string, Base::ptr> _subs;
            //Json::Value _val;
    };
    
    // 数组节点：子节点按加入顺序排列，对应 JSON array
    class Array: public Base {
        public:
            using ptr = std::shared_ptr<Array>;
            // 构造指定 key 的数组节点
            Array(const std::string &key);
            // 向数组追加一个对象元素并返回
            std::shared_ptr<Object> newObject(const std::string &key);
            // 向数组追加一个数组元素并返回
            std::shared_ptr<Array> newArray(const std::string &key);
            // 把元素追加到数组末尾
            void addElement(const Base::ptr &sub);
            // 递归生成数组的 JSON 值
            virtual const Json::Value val() const;
            // 将数组及其元素序列化为 JSON 字符串
            virtual std::string to_string() const;
        private:
            std::vector<Base::ptr> _subs;
    };

    // Elasticsearch 自定义分词器配置节点
    class Tokenizer : public Object{
        public:
            using ptr = std::shared_ptr<Tokenizer>;
            // 按分词器名称构造，默认使用 ik_max_word
            Tokenizer(const std::string &str);
            // 设置底层分词器类型
            void tokenizer(const std::string &t);
            // 设置分词器配置类型，默认 custom
            void type(const std::string &type);
    };
    // Elasticsearch analyzer 配置节点
    class Analyzer : public Object {
        public:
            using ptr = std::shared_ptr<Analyzer>;
            // 构造名为 analyzer 的配置节点
            Analyzer();
            // 获取或创建指定名称的分词器配置
            Tokenizer::ptr tokenizer(const std::string &tokenizer_name);
    };
    // Elasticsearch analysis 配置节点
    class Analysis : public Object {
        public:
            using ptr = std::shared_ptr<Analysis>;
            // 构造名为 analysis 的配置节点
            Analysis();
            // 获取或创建 analyzer 子节点
            Analyzer::ptr analyzer();
    };

    // Elasticsearch settings 配置节点
    class Settings : public Object {
        public:
            using ptr = std::shared_ptr<Settings>;
            // 构造名为 settings 的配置节点
            Settings();
            // 获取或创建 analysis 子节点
            Analysis::ptr analysis();
    };

    // 索引字段的 mapping 定义
    class Field : public Object {
        public:
            using ptr = std::shared_ptr<Field>;
            // 按字段名构造，默认类型为 text
            Field(const std::string &key);
            // 设置字段类型（text/keyword/long 等）
            void type(const std::string &type);
            // 设置字段评分权重
            void boost(double boost);
            // 设置字段是否参与索引
            void index(bool flag);
            // 设置字段使用的分词器名称
            void analyzer(const std::string &analizer_name);
    };

    // 字段属性集合，对应 mapping.properties
    class Properties : public Object {
        public:
            using ptr = std::shared_ptr<Properties>;
            // 构造名为 properties 的配置节点
            Properties();
            // 获取或创建指定字段的 mapping 定义
            Field::ptr field(const std::string &field_name);
    };

    // 索引 mapping 配置节点
    class Mappings : public Object {
        public:
            using ptr = std::shared_ptr<Mappings>;
            // 构造 mapping 节点并默认关闭动态映射
            Mappings();
            // 获取或创建 properties 子节点
            Properties::ptr properties();
            // 设置是否允许动态添加字段
            void dynamic(bool flag);
    };


    // term 精确匹配查询
    class Term : public Object {
        public:
            using ptr = std::shared_ptr<Term>;
            // 按字段名构造 term 查询
            Term(const std::string &field);
            // 设置 term 查询的目标值
            template<typename T>
            void setValue(const T &value) {
                this->add(_field, value);
            }
        private:
            std::string _field;
    };
    // terms 多值精确匹配查询
    class Terms : public Object {
        public:
            using ptr = std::shared_ptr<Terms>;
            // 按字段名构造 terms 查询
            Terms(const std::string &field);
            // 追加一个 terms 查询值
            template<typename T>
            void setValue(const T &value) {
                this->append(_field, value);
            }
        private:
            std::string _field;
    };
    // match 全文匹配查询
    class Match : public Object {
        public:
            using ptr = std::shared_ptr<Match>;
            // 按字段名构造 match 查询
            Match(const std::string &field);
            // 设置 match 查询内容
            template<typename T>
            void setValue(const T &value) {
                this->add(_field, value);
            }
        private:
            std::string _field;
    };
    // multi_match 多字段全文匹配查询
    class MultiMatch : public Object {
        public:
            using ptr = std::shared_ptr<MultiMatch>;
            // 构造 multi_match 查询
            MultiMatch();
            // 追加参与匹配的字段名
            void appendField(const std::string &field);
            // 设置多字段匹配的查询内容
            template<typename T>
            void setQuery(const T &query) {
                this->add("query", query);
            }
    };
    // range 范围查询
    class Range : public Object {
        public:
            using ptr = std::shared_ptr<Range>;
            // 按字段名构造 range 查询
            Range(const std::string &field);
            // 设置范围上下界（gt/lt）
            template<typename T>
            void setRange(const T &gt, const T& lt) {
                _sub->add("gt", gt);
                _sub->add("lt", lt);
            }
        private:
            Object::ptr _sub;
    };

    // 查询对象基类：可创建各类查询子节点
    class QObject : public Object {
        public:
            using ptr = std::shared_ptr<QObject>;
            // 按 key 构造查询对象
            QObject(const std::string &key);
            Term::ptr term(const std::string &field); //创建一个term子对象
            Terms::ptr terms(const std::string &field); //创建一个terms子对象
            Match::ptr match(const std::string &field); //创建一个match子对象
            MultiMatch::ptr multi_match(); //创建一个multi_match子对象
            Range::ptr range(const std::string &field); //创建一个range子对象
    };

    // 查询数组基类：以数组形式组织多个查询子节点
    class QArray : public Array {
        public:
            using ptr = std::shared_ptr<QArray>;
            // 按 key 构造查询数组
            QArray(const std::string &key);
            // 创建 term 查询元素
            Term::ptr term(const std::string &field);
            // 创建 terms 查询元素
            Terms::ptr terms(const std::string &field);
            // 创建 match 查询元素
            Match::ptr match(const std::string &field);
            // 创建 multi_match 查询元素
            MultiMatch::ptr multi_match(); 
            // 创建 range 查询元素
            Range::ptr range(const std::string &field);
    };

    // bool.must 子句集合：所有条件都必须匹配
    class Must : public QArray {
        public:
            using ptr = std::shared_ptr<Must>;
            // 构造 must 子句集合
            Must();
    };
    // bool.should 子句集合：满足任一条件即可
    class Should : public QArray {
        public:
            using ptr = std::shared_ptr<Should>;
            // 构造 should 子句集合
            Should();
    };
    // bool.must_not 子句集合：所有条件都必须不匹配
    class MustNot : public QArray {
        public:
            using ptr = std::shared_ptr<MustNot>;
            // 构造 must_not 子句集合
            MustNot();
    };

    // bool 复合查询，组合 must/should/must_not
    class QBool : public QObject {
        public:
            using ptr = std::shared_ptr<QBool>;
            // 构造 bool 查询
            QBool();
            // 获取或创建 must 子句集合
            Must::ptr must();
            // 获取或创建 should 子句集合
            Should::ptr should();
            // 获取或创建 must_not 子句集合
            MustNot::ptr must_not();
            // 设置 should 子句至少需要匹配的数量
            void minimum_should_match(size_t count);
    };

    // 查询根节点，对应请求体中的 query
    class Query : public QObject {
        public:
            using ptr = std::shared_ptr<Query>;
            // 构造 query 根节点
            Query();
            // 生成 match_all 查询，匹配全部文档
            void match_all();
            // 获取或创建 bool 子查询
            QBool::ptr qbool();
            // 快捷获取 must 子句集合
            Must::ptr must();
            // 快捷获取 should 子句集合
            Should::ptr should();
            // 快捷获取 must_not 子句集合
            MustNot::ptr must_not();
    };

    
    // Elasticsearch 请求元信息：索引、类型、操作和文档 ID
    class Request {
        public:
            // 构造请求元信息
            Request(const std::string &index, const std::string &type, const std::string &op, const std::string &id);
            // 设置索引名称
            void set_index(const std::string &index);
            // 设置文档类型
            void set_type(const std::string &type);
            // 设置操作类型
            void set_op(const std::string &op);
            // 设置文档 ID
            void set_id(const std::string &id);
            // 返回索引名称
            const std::string& index() const;
            // 返回文档类型
            const std::string& type() const;
            // 返回操作类型
            const std::string& op() const;
            // 返回文档 ID
            const std::string& id() const;
        protected:
            std::string _index;
            std::string _type;
            std::string _op;
            std::string _id;
    };

    // 索引创建请求：组合 settings 与 mapping
    class Indexer : public Object, public Request {
        public:
            using ptr = std::shared_ptr<Indexer>;
            // 构造索引创建请求
            Indexer(const std::string &index);
            // 获取或创建 settings 配置
            Settings::ptr settings();
            // 获取或创建索引使用的分词器
            Tokenizer::ptr tokenizer(const std::string &tokenizer_name);
            // 获取或创建 mapping 配置
            Mappings::ptr mappings();
            // 获取或创建字段 mapping
            Field::ptr field(const std::string &field_name);
    };

    // 文档插入请求
    class Inserter : public Object, public Request {
        public:
            using ptr = std::shared_ptr<Inserter>;
            // 构造文档插入请求
            Inserter(const std::string &index, const std::string &id);
    };

    // 文档更新请求，支持 doc 局部更新
    class Updater : public Object, public Request {
        public:
            using ptr = std::shared_ptr<Updater>;
            // 构造文档更新请求
            Updater(const std::string &index, const std::string &id);
            // 获取或创建 doc 更新对象
            Object::ptr doc();
            // 向 doc 中添加字段值
            template<typename T>
            void add(const std::string &key, const T &val) {
                this->doc()->add(key, val);
            }
            // 向 doc 中追加数组字段值
            template<typename T>
            void append(const std::string &key, const T &val) {
                this->doc()->append(key, val);
            }
    };
    // 文档删除请求
    class Deleter : public Object, public Request {
        public:
            using ptr = std::shared_ptr<Deleter>;
            // 构造文档删除请求
            Deleter(const std::string &index, const std::string &id);
    };
    // 文档搜索请求
    class Searcher : public Object, public Request  {
        public:
            // 构造文档搜索请求
            Searcher(const std::string &index);
            // 获取或创建 query 查询根节点
            Query::ptr query();
            // 设置返回结果数量
            void size(size_t count);
            // 设置结果起始偏移量
            void from(size_t offset);
    };

    // Elasticsearch 客户端抽象接口
    class BaseClient {
        public:
            using ptr = std::shared_ptr<BaseClient>;
            // 默认构造
            BaseClient() = default;
            // 虚析构，保证派生类正确释放
            virtual ~BaseClient() = default;
            virtual bool create(const Indexer &idx) = 0; //创建索引
            virtual bool insert(const Inserter &ins) = 0; //插入数据
            virtual bool update(const Updater &upd) = 0; //更新数据
            virtual bool remove(const Deleter &del) = 0; //删除数据
            virtual bool remove(const std::string &index) = 0; //删除索引
            virtual std::optional<Json::Value> search(const Searcher &sea) = 0; //查询数据
    };

    // 基于 elasticlient 的 Elasticsearch 客户端实现
    class ESClient : public BaseClient {
        public:
            using ptr = std::shared_ptr<ESClient>;
            // 按节点地址列表创建 ES 客户端
            ESClient(const std::vector<std::string> &hosts);
            // 创建索引
            bool create(const Indexer &idx) override;
            // 插入文档
            bool insert(const Inserter &ins) override;
            // 更新文档
            bool update(const Updater &upd) override;
            // 删除文档
            bool remove(const Deleter &del) override;
            // 删除索引
            bool remove(const std::string &index) override;
            // 搜索文档并返回 _source 列表
            std::optional<Json::Value> search(const Searcher &sea) override;
        private:
            std::shared_ptr<elasticlient::Client> _client;
    };
}