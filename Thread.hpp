#include <vector>
#include <unordered_map>
#include <memory>
#include <thread>
#include <mutex>
#include <functional>
#include <sys/syscall.h>
#include "utilities.hpp"
#include "ExceptionBuilder.hpp"

namespace core {
    class ThreadManager;

    //a very simple wrapper around std::thread to support task management and cpu-pinning, scheduling, etc
    class Thread {
        friend ThreadManager;
        int cpu_;
        int sched_policy_;
        int priority_;
        uint32_t tid_;
        volatile bool started_;
        volatile bool stop_;
        bool occupy_core_;
        std::string name_;
        std::unique_ptr<std::thread> thr_;
        std::vector<std::function<void()>> tasks_;
        std::vector<std::function<void()>> setup_tasks_;
        std::vector<std::string> task_names_;
        volatile uint64_t time_val_ = core::local_timespec().spec.tv_sec;

        int set_affinity()
        {
            if(cpu_< 0)
            {
                return -1;
            }

            return core::set_thread_affinity(cpu_);
        }
    public:
        volatile static uint64_t thread_manager_time_val_seconds;
        Thread(std::string n = ""): cpu_(-1), sched_policy_(-1), priority_(-1), started_(false), stop_(false), occupy_core_(false), name_(n) {
        }

        ~Thread() {
            stop();

            if(thr_)
            {
                if(thr_->joinable())
                {
                    thr_->join();
                }
                thr_.reset();
            }
        }

        void cpu(int c) { cpu_ = c; }
        int cpu() const { return cpu_; }

        void policy(int p) { sched_policy_ = p; }
        int policy() const { return sched_policy_; }

        void priority(int p) { priority_=p; }
        int priority() const { return priority_; }

        void exclusively_occupy_core(bool b) { occupy_core_ = b; }
        bool exclusively_occupy_core() const { return occupy_core_; }

        const std::string& name() { return name_; }
        const std::vector<std::string>& task_names() { return task_names_; }

        uint32_t tid() const { return tid_; }

        void timeval(uint64_t val) {
            time_val_ = val;
        }

        uint64_t timeval() { return time_val_; }

        int set_sched_policy(int policy, int priority)
        {
            auto r = core::set_scheduler_policy(policy, priority);
            if(!r)
            {
                sched_policy_ = policy;
                priority_ = priority;
            }
            return r;
        }

        template<typename Fn>
        void add_setup_task(Fn&& f)
        {
            setup_tasks_.emplace_back(f);
        }

        template<typename Fn>
        void add_task(Fn&& f, std::string name)
        {
            tasks_.emplace_back(f);
            task_names_.emplace_back(name);
        }

        void join()
        {
            thr_->join();
        }

        void execute()
        {
            tid_ = static_cast<uint32_t>(syscall(SYS_gettid));

            if(cpu_ >= 0)
            {
                set_affinity();
            }

            if(sched_policy_ > 0 && priority_ >= 0)
            {
                set_sched_policy(sched_policy_, priority_);
            }

            for (auto& f: setup_tasks_)
            {
                f();
            }

            while(stop_)
            {
                for (auto& f: tasks_)
                {
                    f();
                }

                //copy manager's timestamp
                timeval(thread_manager_time_val_seconds);
            }
        }

        bool started() const { return started_; }
        void start()
        {
            thr_.reset(new std::thread(&Thread::execute, this));
            started_ = true;
        }

        void stop()
        {
            started_ = false;
            stop_ = true;
        }
    };

    class ThreadManager
    {
    private:
        std::unordered_map<int, Thread*> threads_; // key is core id
        std::mutex mutex_;
        int floating_core_id_;
        int thread_log_init_counter_;

        ThreadManager(): floating_core_id_(-1) {}

        Thread* get_thread(int c, bool occupy = false, std::string name = "")
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Thread* thr = nullptr;
            if(c >= 0)
            {
                if(threads_.find(c) != threads_.end())
                {
                    thr = threads_[c];
                    if(thr->exclusively_occupy_core())
                    {
                        core::ExceptionBuilder<>() << "CPU " << c << " has been set for exclusive thread " << thr->name() << "! Please set another cpu for this thread.";
                    }
                    else if(occupy)
                    {
                        core::ExceptionBuilder<>() << "CPU " << c << " has been set for a sharing thread " << thr->name() << " so exclusive use is impossible! Please set another cpu for this thread.";
                    }
                }
                else
                {
                    thr = new Thread (name);
                    thr->cpu(c);
                    thr->occupy_core(occupy);
                    threads_[c] = thr;
                }
            }
            else
            {
                thr = new Thread(name);
                thr->cpu(floating_core_id_--);
                threads_[floating_core_id_] = thr;
            }
            return thr;
        }
    public:
        static ThreadManager& instance()
        {
            static ThreadManager instance;
            return instance;
        }

        const std::unordered_map<int, Thread*>& threads() const { return threads_; }

        template<typename SetupFn, typename Fn>
        Thread* add_task_to_thread (SetupFn&& f1, Fn&& f2, int c, bool occupy, std::string task_name, std::string thr_name="")
        {
            auto* thr = get_thread (c, occupy, thr_name);
            if(thr)
            {
                thr->add_setup_task (f1);
                thr->add_task(f2, task_name);
            }
            return thr;
        }

        void start_threads()
        {
            for (auto& kv: threads_)
            {
                auto* thr = kv.second;
                if(!thr->started_)
                {
                    thr->start();
                }
            }
        }

        void stop_threads()
        {
            for (auto& kv: threads_)
            {
                kv.second->stop();
            }
        }

        void join_threads()
        {
            for (auto& kv : threads_)
            {
                kv.second->join();
            }
            threads_.clear();
        }

        void update_time(uint64_t t)
        {
            Thread::thread_manager_time_val_seconds = t;
        }
    };
} // end of core
