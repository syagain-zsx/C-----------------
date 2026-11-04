#include "elastic.h"
#include "util.h"
#include "log.h"
#include <optional>

namespace JianLiuIcs {
    
    // 构造基类节点并保存 key
    Base::Base(const std::string &key): _key(key) {}
    // 返回节点在父对象中的键名
    const std::string& Base::key() const { return _key; }
    // 返回节点自身的 JSON 值
    const Json::Value Base::val() const { return _val; }
    // 把节点序列化为带 key 的 JSON 字符串
    std::string Base::to_string() const { 
        Json::Value root;
        root[_key] = _val;
        return *JianLiuUtil::JSON::serialize(root, true); 
    }
    
    // 构造对象节点
    Object::Object(const std::string &key): Base(key) {}
    // 按 key 获取或创建对象子节点
    std::shared_ptr<Object> Object::newObject(const std::string &key) {
        Object::ptr sub;
        sub = this->getElement<Object>(key);
        if (sub) {
            return sub;
        }
        sub = std::make_shared<Object>(key);
        this->addElement(sub);
        return sub;
    }
    // 按 key 获取或创建数组子节点
    std::shared_ptr<Array> Object::newArray(const std::string &key) {
        Array::ptr array;
        array = this->getElement<Array>(key);
        if (array) {
            return array;
        }
        array = std::make_shared<Array>(key);
        this->addElement(array);
        return array;
    }
    // 把子节点按 key 放入对象
    void Object::addElement(const Base::ptr &sub) {
        _subs[sub->key()] = sub;
    }
    // 合并自身字段与子节点值生成 JSON
    const Json::Value Object::val() const { 
        Json::Value root = _val; //将当前val的数据赋值给root
        for (auto &i: _subs) {   //当前对象也可能有自己子对象，把子对象的val拿出来赋值到root
            root[i.first] = i.second->val();
        }
        return root;
    }
    // 序列化对象及其子节点
    std::string Object::to_string() const {
        Json::Value root = _val; //先拿本身的数据
        for (auto &i: _subs) {   //再拿自己的子对象的数据
            root[i.first] = i.second->val();
        }
        return *JianLiuUtil::JSON::serialize(root, true); 
    }


    // 构造数组节点
    Array::Array(const std::string &key): Base(key) {}
    // 向数组追加对象元素
    std::shared_ptr<Object> Array::newObject(const std::string &key) {
        auto obj = std::make_shared<Object>(key);
        this->addElement(obj);
        return obj;
    }
    // 向数组追加数组元素
    std::shared_ptr<Array> Array::newArray(const std::string &key) {
        auto arr = std::make_shared<Array>(key);
        this->addElement(arr);
        return arr;
    }
    // 把元素追加到数组末尾
    void Array::addElement(const Base::ptr &sub) {
        _subs.push_back(sub);
    }
    // 生成数组的 JSON 值
    const Json::Value Array::val() const {
        Json::Value root = _val;
        for (auto &i: _subs) {
            root.append(i->val());
        }
        return root;
    }
    // 序列化数组及其元素
    std::string Array::to_string() const {
        Json::Value root = _val;
        for (auto &i: _subs) {
            root.append(i->val());
        }
        return *JianLiuUtil::JSON::serialize(root, true);
    }

    
    // 按分词器名称构造，默认使用 ik_max_word
    Tokenizer::Tokenizer(const std::string &str): Object(str) {
        this->tokenizer("ik_max_word");
        this->type("custom");
    }
    // 设置底层分词器类型
    void Tokenizer::tokenizer(const std::string &t) {
        this->add("tokenizer", t);
    }
    // 设置分词器配置类型
    void Tokenizer::type(const std::string &type) {
        this->add("type", type);
    }

    // 构造 analyzer 配置节点
    Analyzer::Analyzer(): Object("analyzer") {}
    // 获取或创建指定名称的分词器
    Tokenizer::ptr Analyzer::tokenizer(const std::string &tokenizer_name) {
        Tokenizer::ptr tokenizer;
        tokenizer = this->getElement<Tokenizer>(tokenizer_name);
        if (tokenizer) {
            return tokenizer;
        }
        tokenizer = std::make_shared<Tokenizer>(tokenizer_name);
        this->addElement(tokenizer);
        return tokenizer;
    }
    
    // 构造 analysis 配置节点
    Analysis::Analysis(): Object("analysis") {}
    // 获取或创建 analyzer 子节点
    Analyzer::ptr Analysis::analyzer() {
        Analyzer::ptr analyzer;
        analyzer = this->getElement<Analyzer>("analyzer");
        if (analyzer) {
            return analyzer;
        }
        analyzer = std::make_shared<Analyzer>();
        this->addElement(analyzer);
        return analyzer;
    }

    // 构造 settings 配置节点
    Settings::Settings(): Object("settings") {}
    // 获取或创建 analysis 子节点
    Analysis::ptr Settings::analysis() {
        Analysis::ptr analysis;
        analysis = this->getElement<Analysis>("analysis"); 
        if (analysis) {
            return analysis;
        }
        analysis = std::make_shared<Analysis>();
        this->addElement(analysis);
        return analysis;
    }

    
    // 按字段名构造，默认类型为 text
    Field::Field(const std::string &key): Object(key){
        this->type("text");
    }
    // 设置字段类型
    void Field::type(const std::string &type) {
        this->add("type", type);
    }
    // 设置字段评分权重
    void Field::boost(double boost) {
        this->add("boost", boost);
    }
    // 设置字段是否参与索引
    void Field::index(bool flag) {
        this->add("index", flag);
    }
    // 设置字段使用的分词器
    void Field::analyzer(const std::string &analizer_name) {
        this->add("analyzer", analizer_name);
    }

    // 构造 properties 配置节点
    Properties::Properties(): Object("properties") {}
    // 获取或创建指定字段的 mapping
    Field::ptr Properties::field(const std::string &field_name) {
        Field::ptr field;
        field = this->getElement<Field>(field_name);
        if (field) {
            return field;
        }
        field = std::make_shared<Field>(field_name);
        this->addElement(field);
        return field;
    }

    // 构造 mapping 节点并默认关闭动态映射
    Mappings::Mappings(): Object("mapping") {
        this->dynamic(false);
    }
    // 设置是否允许动态映射
    void Mappings::dynamic(bool flag) {
        this->add("dynamic", flag);
    }
    // 获取或创建 properties 子节点
    Properties::ptr Mappings::properties() {
        Properties::ptr properties;
        properties = this->getElement<Properties>("properties");
        if (properties) {
            return properties;
        }
        properties = std::make_shared<Properties>();
        this->addElement(properties);
        return properties;
    }

    // 构造 term 查询
    Term::Term(const std::string &field): Object("term"), _field(field) {}
    // 构造 terms 查询
    Terms::Terms(const std::string &field): Object("terms"), _field(field) {}
    // 构造 match 查询
    Match::Match(const std::string &field): Object("match"), _field(field) {}
    // 构造 multi_match 查询
    MultiMatch::MultiMatch():Object("multi_match") {}
    // 追加参与匹配的字段
    void MultiMatch::appendField(const std::string &field) { this->append("fields", field); }
    // 构造 range 查询并创建字段子节点
    Range::Range(const std::string &field): Object("range") {
        _sub = std::make_shared<Object>(field);
        this->addElement(_sub);
    }

    
    // 构造查询对象
    QObject::QObject(const std::string &key): Object(key) {}
    // 获取或创建 term 查询子节点
    Term::ptr QObject::term(const std::string &field) {
        Term::ptr term;
        term = this->getElement<Term>("term");
        if (term) {
            return term;
        }
        term = std::make_shared<Term>(field);
        this->addElement(term);
        return term;
    }
    // 获取或创建 terms 查询子节点
    Terms::ptr QObject::terms(const std::string &field) {
        Terms::ptr terms;
        terms = this->getElement<Terms>("terms");
        if (terms) {
            return terms;
        }
        terms = std::make_shared<Terms>(field);
        this->addElement(terms);
        return terms;
    }
    // 获取或创建 match 查询子节点
    Match::ptr QObject::match(const std::string &field) {
        Match::ptr match;
        match = this->getElement<Match>("match");
        if (match) {
            return match;
        }
        match = std::make_shared<Match>(field);
        this->addElement(match);
        return match;
    }
    // 获取或创建 multi_match 查询子节点
    MultiMatch::ptr QObject::multi_match() {
        MultiMatch::ptr multi_match;
        multi_match = this->getElement<MultiMatch>("multi_match");
        if (multi_match) {
            return multi_match;
        }
        multi_match = std::make_shared<MultiMatch>();
        this->addElement(multi_match);
        return multi_match;
    }
    // 获取或创建 range 查询子节点
    Range::ptr QObject::range(const std::string &field) {
        Range::ptr range;
        range = this->getElement<Range>("range");
        if (range) {
            return range;
        }
        range = std::make_shared<Range>(field);
        this->addElement(range);
        return range;
    }

    
    // 构造查询数组
    QArray::QArray(const std::string &key): Array(key) {}
    // 创建 term 查询元素
    Term::ptr QArray::term(const std::string &field) {
        auto tmp = std::make_shared<Term>(field);
        auto obj = this->newObject("");
        obj->addElement(tmp);
        return tmp;
    }
    // 创建 terms 查询元素
    Terms::ptr QArray::terms(const std::string &field) {
        auto tmp = std::make_shared<Terms>(field);
        auto obj = this->newObject("");
        obj->addElement(tmp);
        return tmp;
    }
    // 创建 match 查询元素
    Match::ptr QArray::match(const std::string &field) {
        auto tmp = std::make_shared<Match>(field);
        auto obj = this->newObject("");
        obj->addElement(tmp);
        return tmp;
    }
    // 创建 multi_match 查询元素
    MultiMatch::ptr QArray::multi_match() {
        auto tmp = std::make_shared<MultiMatch>();
        auto obj = this->newObject("");
        obj->addElement(tmp);
        return tmp;
    }
    // 创建 range 查询元素
    Range::ptr QArray::range(const std::string &field) {
        auto tmp = std::make_shared<Range>(field);
        auto obj = this->newObject("");
        obj->addElement(tmp);
        return tmp;
    }

    // 构造 must 子句集合
    Must::Must(): QArray("must") {}
    // 构造 should 子句集合
    Should::Should(): QArray("should") {}
    // 构造 must_not 子句集合
    MustNot::MustNot(): QArray("must_not") {}
    
    // 构造 bool 查询
    QBool::QBool(): QObject("bool") {}
    // 设置 should 子句至少匹配的数量
    void QBool::minimum_should_match(size_t count) {
        this->add("minimum_should_match", count);
    }
    // 获取或创建 must 子句集合
    Must::ptr QBool::must() {
        Must::ptr tmp;
        tmp = this->getElement<Must>("must");
        if (tmp) {
            return tmp;
        }
        tmp = std::make_shared<Must>();
        this->addElement(tmp);
        return tmp;
    }
    // 获取或创建 should 子句集合
    Should::ptr QBool::should() {
        Should::ptr tmp;
        tmp = this->getElement<Should>("should");
        if (tmp) {
            return tmp;
        }
        tmp = std::make_shared<Should>();
        this->addElement(tmp);
        return tmp;
    }
    // 获取或创建 must_not 子句集合
    MustNot::ptr QBool::must_not()  {
        MustNot::ptr tmp;
        tmp = this->getElement<MustNot>("must_not");
        if (tmp) {
            return tmp;
        }
        tmp = std::make_shared<MustNot>();
        this->addElement(tmp);
        return tmp;
    }






    // 构造 query 根节点
    Query::Query(): QObject("query"){}
    // 生成 match_all 查询
    void Query::match_all() {
        this->add("match_all", Json::Value(Json::ValueType::objectValue));
    }
    // 获取或创建 bool 子查询
    QBool::ptr Query::qbool()  {
        QBool::ptr tmp;
        tmp = this->getElement<QBool>("bool");
        if (tmp) {
            return tmp;
        }
        tmp = std::make_shared<QBool>();
        this->addElement(tmp);
        return tmp;
    }
    // 快捷获取 must 子句集合
    Must::ptr Query::must() {
        return this->qbool()->must();
    }
    // 快捷获取 should 子句集合
    Should::ptr Query::should() {
        return this->qbool()->should();
    }
    // 快捷获取 must_not 子句集合
    MustNot::ptr Query::must_not() {
        return this->qbool()->must_not();
    }


    // 构造请求元信息
    Request::Request(const std::string &index, const std::string &type, const std::string &op, const std::string &id)
        : _index(index), _type(type), _op(op), _id(id) {}
    // 设置索引名称
    void Request::set_index(const std::string &index) { _index = index; }
    // 设置文档类型
    void Request::set_type(const std::string &type) { _type = type; }
    // 设置操作类型
    void Request::set_op(const std::string &op) { _op = op; }
    // 设置文档 ID
    void Request::set_id(const std::string &id) { _id = id; }
    // 返回索引名称
    const std::string& Request::index() const { return _index; }
    // 返回文档类型
    const std::string& Request::type() const { return _type; }
    // 返回操作类型
    const std::string& Request::op() const { return _op; }
    // 返回文档 ID
    const std::string& Request::id() const { return _id; }

    // 构造索引创建请求
    Indexer::Indexer(const std::string &index): Object(""), Request(index, "_doc", "index", index) {}
    // 获取或创建 settings 配置
    Settings::ptr Indexer::settings() {
        Settings::ptr settings;
        settings = this->getElement<Settings>("settings");
        if (settings) {
            return settings;
        }
        settings = std::make_shared<Settings>();
        this->addElement(settings);
        return settings;
    }
    // 获取或创建索引分词器配置
    Tokenizer::ptr Indexer::tokenizer(const std::string &tokenizer_name) {
        return this->settings()->analysis()->analyzer()->tokenizer(tokenizer_name);
    }
    // 获取或创建 mapping 配置
    Mappings::ptr Indexer::mappings() {
        Mappings::ptr mappings;
        mappings = this->getElement<Mappings>("mapping");
        if (mappings) {
            return mappings;
        }
        mappings = std::make_shared<Mappings>();
        this->addElement(mappings);
        return mappings;
    }
    // 获取或创建字段 mapping
    Field::ptr Indexer::field(const std::string &field_name) {
        return this->mappings()->properties()->field(field_name);
    }

    // 构造文档插入请求
    Inserter::Inserter(const std::string &index, const std::string &id): Object(""), Request(index, "_doc", "_insert", id) {}
    // 构造文档更新请求
    Updater::Updater(const std::string &index, const std::string &id): Object(""), Request(index, "_doc", "_update", id) {}
    // 获取或创建 doc 更新对象
    Object::ptr Updater::doc() {
        Object::ptr doc;
        doc = this->getElement<Object>("doc");
        if (doc) {
            return doc;
        }
        doc = std::make_shared<Object>("doc");
        this->addElement(doc);
        return doc;
    }
    // 构造文档删除请求
    Deleter::Deleter(const std::string &index, const std::string &id): Object(""), Request(index, "_doc", "_delete", id) {}
    // 构造文档搜索请求
    Searcher::Searcher(const std::string &index): Object(""), Request(index, "_doc", "_search", "") {}
    // 获取或创建 query 查询根节点
    Query::ptr Searcher::query() {
        Query::ptr query;
        query = this->getElement<Query>("query");
        if (query) {
            return query;
        }
        query = std::make_shared<Query>();
        this->addElement(query);
        return query;
    }
    // 设置返回结果数量
    void Searcher::size(size_t count) {
        this->add("size", count);
    }
    // 设置结果起始偏移量
    void Searcher::from(size_t offset) {
        this->add("from", offset);
    }



    // 按节点地址列表创建 ES 客户端
    ESClient::ESClient(const std::vector<std::string> &hosts)
        : _client(std::make_shared<elasticlient::Client>(hosts)) {}
    // 创建索引并判断响应状态
    bool ESClient::create(const Indexer &idx) {
        //1. 获取关键信息
        std::string index_name = idx.index();
        std::string index_type = idx.type();
        std::string index_id = idx.id();
        std::string index_body = idx.to_string();
        //2. 发起请求
        auto resp = _client->index(index_name, index_type, index_id, index_body);
        //3. 收到响应，判断是否成功
        if (resp.status_code < 200 || resp.status_code >= 300) {
            ERR("创建索引失败: {}/{}/{}/{} - {}", index_name, index_type, index_id, index_body, resp.text);
            return false;
        }
        return true;
    }
    // 插入文档并判断响应状态
    bool ESClient::insert(const Inserter &ins) {
        //1. 获取关键信息
        std::string index_name = ins.index();
        std::string index_type = ins.type();
        std::string doc_id = ins.id();
        std::string index_body = ins.to_string();
        auto resp = _client->index(index_name, index_type, doc_id, index_body);
        //3. 收到响应，判断是否成功
        if (resp.status_code < 200 || resp.status_code >= 300) {
            ERR("新增数据失败: {}/{}/{}/{} - {}", index_name, index_type, doc_id, index_body, resp.text);
            return false;
        }
        return true;
    }
    // 更新文档并判断响应状态
    bool ESClient::update(const Updater &upd) {
        //1. 获取关键信息
        std::string index_name = upd.index();
        std::string index_type = upd.type();
        std::string doc_id = upd.id();
        std::string index_body = upd.to_string();
        std::string url = index_name + "/_update/"  + doc_id;
        auto resp = _client->performRequest(elasticlient::Client::HTTPMethod::POST, url, index_body);
        if (resp.status_code < 200 || resp.status_code >= 300) {
            ERR("更新数据失败: {}/{}/{} - {}", index_name, url, index_body, resp.text);
            return false;
        }
        return true;
    }
    // 删除文档并判断响应状态
    bool ESClient::remove(const Deleter &del) {
        std::string index_name = del.index();
        std::string index_type = del.type();
        std::string doc_id = del.id();
        auto resp = _client->remove(index_name, index_type, doc_id);
        if (resp.status_code < 200 || resp.status_code >= 300) {
            ERR("删除数据失败: {}/{}/{} - {}", index_name, index_type, doc_id, resp.text);
            return false;
        }
        return true;
    }   
    // 删除整个索引并判断响应状态
    bool ESClient::remove(const std::string &index_name) {
        auto resp = _client->performRequest(elasticlient::Client::HTTPMethod::DELETE, index_name, "");
        if (resp.status_code < 200 || resp.status_code >= 300) {
            ERR("删除索引失败: {} - {}", index_name, resp.text);
            return false;
        }
        return true;
    }
    // 搜索文档并提取 _source 列表
    std::optional<Json::Value> ESClient::search(const Searcher &sea) {
        //1. 获取关键信息
        std::string index_name = sea.index();
        std::string index_type = sea.type();
        std::string index_body = sea.to_string();
        auto resp = _client->search(index_name, index_type, index_body);
        if (resp.status_code < 200 || resp.status_code >= 300) {
            ERR("搜索数据失败: {}/{}/{} - {}", index_name, index_type, index_body, resp.text);
            return std::optional<Json::Value>();
        }
        auto json_resp = JianLiuUtil::JSON::unserialize(resp.text);
        if (!json_resp) {
            ERR("搜索数据,对响应进行反序列化失败: {}/{}/{} - {}", index_name, index_type, index_body, resp.text);
            return std::optional<Json::Value>();
        }
        if ((*json_resp).isNull() || (*json_resp)["hits"].isNull() || (*json_resp)["hits"]["hits"].isNull()) {
            ERR("搜索数据,响应格式错误: {}/{}/{} - {}", index_name, index_type, index_body, resp.text);
            return std::optional<Json::Value>();
        }
        Json::Value result;
        int sz = (*json_resp)["hits"]["hits"].size();
        for (int i = 0; i < sz; ++i) {
            result.append((*json_resp)["hits"]["hits"][i]["_source"]);
        }
        return result;
    }

}