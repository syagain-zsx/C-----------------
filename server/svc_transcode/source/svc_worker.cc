#include "svc_worker.h"

namespace svc_transcode {
    SvcWorker::SvcWorker(size_t thread_count): _thread_count(thread_count), _is_stop(false) {
        if (_thread_count == 0) _thread_count = 1;
        for (size_t i = 0; i < _thread_count; ++i) {
            _threads.emplace_back(std::thread(&SvcWorker::threadEntry, this));
        }
    }
    SvcWorker::~SvcWorker() {
        // 先通知退出，再 join；否则 wait 谓词永远不会成立，析构会永久挂起
        stop();
        for (auto &t : _threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    }
    void SvcWorker::addTask(Task &&task) {
        {
            std::unique_lock<std::mutex> lock(_mtx);
            if (_is_stop) {
                return;
            }
            _tasks.push(std::move(task));
        }
        _cv.notify_one();
    }
    void SvcWorker::stop() {
        {
            std::unique_lock<std::mutex> lock(_mtx);
            _is_stop = true;
        }
        _cv.notify_all();
    }

    void SvcWorker::threadEntry() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(_mtx);
                // 退出条件：停止且队列已空；停止时仍可把已入队的任务跑完
                _cv.wait(lock, [this]() { return _is_stop || !_tasks.empty(); });
                if (_is_stop && _tasks.empty()) {
                    break;
                }
                task = std::move(_tasks.front());
                _tasks.pop();
            }
            if (task) {
                try {
                    task();
                } catch (const std::exception &e) {
                    std::cerr << "转码任务异常: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "转码任务未知异常" << std::endl;
                }
            }
        }
    }
}
