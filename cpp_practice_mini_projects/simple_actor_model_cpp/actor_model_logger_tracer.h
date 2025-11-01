#ifndef ACTOR_MODEL_LOGGER_TRACER_H
#define ACTOR_MODEL_LOGGER_TRACER_H

#include <iostream>
#include <iomanip>
#include <mutex>
#include <chrono>
#include <sstream>
#include <thread>
#include <fstream>
#include <string>
#include <time.h>
#include "../simple_mpmc_queue/mpmc_queue_bounded.h"

namespace ActorModel
{

    namespace Logger{

        enum class Level{
            Debug, Info, Warn, Error, Fatal };

        constexpr std::string_view level_to_str(Level level)
        {
            switch(level)
            {
                case Level::Debug:
                return "DEBUG";
                case Level::Info:
                return "INFO";
                case Level::Warn:
                return "WARN";
                case Level::Error:
                return "ERROR";
                case Level::Fatal:
                return "FATAL";
            }
            return "INVALID";
        }

        inline std::mutex& log_mtx()
        {
            static std::mutex mtx;
            return mtx;
        }

        inline void log(Level level, std::string_view component, std::string_view log_msg)
        {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm tstamp;
            localtime_r(&now,&tstamp);

            std::ostringstream oss;

            oss << std::put_time(&tstamp,"%F %T") << " [" << level_to_str(level) << "]\t" << component << ": " << log_msg << "\n";

            std::lock_guard<std::mutex> log_lock(log_mtx());
            std::cout << oss.str();
        }
    };

    namespace Profile
    {
        using namespace std::chrono;

        enum class EventType{
            Register,
            Unregister,
            Restart,
            Fail,
            DrainStart,
            DrainEnd,
            Enqueue,
            Dequeue,
            MaxEvent
        };

        inline constexpr std::string_view evtToStr(EventType et)
        {   
            switch(et)
            {
                case EventType::Register:
                    return "Register";
                case EventType::Unregister:
                    return "Unregister";
                case EventType::Restart:
                    return "Restart";
                case EventType::Fail:
                    return "Fail";
                case EventType::DrainStart:
                    return "DrainStart";
                case EventType::DrainEnd:
                    return "DrainEnd";
                case EventType::Enqueue:
                    return "Enqueue";
                case EventType::Dequeue:
                    return "Dequeue";
                case EventType::MaxEvent:
                    return "INVALID";
            }
            return "INVALID";
        }

        struct Event {
            EventType type;
            uint64_t time_stamp;
            std::string component;
            size_t actor_id;
            uint64_t comp_gen;
            std::thread::id thread_id;
            uint32_t payload;

            Event(EventType etype, size_t act_id,uint32_t payload_): type(etype),actor_id(act_id),payload(payload_) 
            {
                time_stamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
                thread_id = std::this_thread::get_id();
            }

        };

        class Profiler
        {
        
        private:
            std::fstream log_file;
            std::string log_file_name;
            std::mutex prof_mtx_;
            std::atomic<bool> trace_enabled;


            Profiler(): trace_enabled{false}
            {
                auto timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
                log_file_name = "log/actor_trace_"+ std::to_string(timestamp)+".csv";

                std::lock_guard<std::mutex> prof_lock(prof_mtx_);
                log_file.open(log_file_name,std::ios::out);
                log_file <<"timestamp,actor_id,thread_id,eventType\n";
            }

        public:
            inline static Profiler& instance()
            {
                static Profiler prof;
                return prof;
            }

            void enableTrace()
            {
                trace_enabled.store(true, std::memory_order_relaxed);
            }

            void disableTrace()
            {
                trace_enabled.store(false,std::memory_order_relaxed);
            }

            inline void record(const Event& ev)
            {
                if (Profile::Profiler::instance().trace_enabled)
                {
                    std::lock_guard<std::mutex> prof_lock(prof_mtx_);
                    log_file << ev.time_stamp<<","<<ev.actor_id<<","<< ev.thread_id << ","<< evtToStr(ev.type) <<"\n"  ; 
                }
            }

            void dump_csv()
            {
                
            }
        };
    };
}

#endif /* ACTOR_MODEL_LOGGER_TRACER */