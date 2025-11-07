#pragma once

#include <cstddef>
#include <functional>
#include <future>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <stop_token>
#include <vector>
#include <stdexcept>

namespace cclone::infra {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t nthreads = std::jthread::hardware_concurrency());
    ~ThreadPool();

    // Удалить копирование и присваивание
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Запуск задачи без возврата
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> void;

    // Запуск задачи с возвратом (future)
    template<typename F, typename... Args>
    auto enqueue_with_future(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>;

    // Блокирующее ожидание завершения всех задач
    void wait();

private:
    using Task = std::packaged_task<void()>;

    std::vector<std::jthread> workers_;
    std::queue<Task> tasks_;
    mutable std::mutex queue_mutex_;
    std::condition_variable_any cv_;
    bool stop_ = false;
};

// =============== Реализация шаблонов ===============

template<typename F, typename... Args>
void ThreadPool::enqueue(F&& f, Args&&... args) {
    enqueue_with_future(std::forward<F>(f), std::forward<Args>(args)...);
    // Игнорируем future — задача запущена
}

template<typename F, typename... Args>
auto ThreadPool::enqueue_with_future(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>
{
    using ReturnType = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

    if (stop_) {
        throw std::runtime_error("ThreadPool is stopped");
    }

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    auto future = task->get_future();
    {
        std::lock_guard lock(queue_mutex_);
        tasks_.emplace([task]() { (*task)(); });
    }
    cv_.notify_one();
    return future;
}

} // namespace cclone::infra

namespace cclone::infra {

ThreadPool::ThreadPool(std::size_t nthreads)
    : stop_(false)
{
    if (nthreads == 0) nthreads = 1;
    workers_.reserve(nthreads);

    for (std::size_t i = 0; i < nthreads; ++i) {
        workers_.emplace_back([this](std::stop_token st) {
            while (!st.stop_requested()) {
                Task task;
                {
                    std::unique_lock lock(queue_mutex_);
                    cv_.wait(lock, [this, &st] {
                        return st.stop_requested() || !tasks_.empty() || stop_;
                    });

                    if (st.stop_requested() || stop_) {
                        // Очистка оставшихся задач (опционально)
                        while (!tasks_.empty()) {
                            tasks_.front()();
                            tasks_.pop();
                        }
                        break;
                    }

                    if (!tasks_.empty()) {
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                }

                if (task.valid()) {
                    task();
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard lock(queue_mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    // jthread автоматически вызовет request_stop и join
}

void ThreadPool::wait() {
    std::unique_lock lock(queue_mutex_);
    cv_.wait(lock, [this] {
        return tasks_.empty();
    });
}

} // namespace cclone::infra