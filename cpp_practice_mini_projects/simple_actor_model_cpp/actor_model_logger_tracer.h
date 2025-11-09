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
#include <filesystem>
#include "../simple_mpmc_queue/mpmc_queue_bounded.h"

/**
 * TODO:
 * 1. Add payload and component info to trace logs
 * 2. Add better way to log thread_id
 * 3. Add condition variable for flush
 * 4. Add some trace filters
 * 5. Add unique id to match log and trace events
 */

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
            StopSystem,
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
                case EventType::StopSystem:
                    return "StopSystem";
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
            uint64_t gen_id;
            std::thread::id thread_id;
            uint32_t payload;

            Event(){}
            Event(EventType etype, size_t act_id,uint64_t genid, uint32_t payload_): 
                type(etype),actor_id(act_id),gen_id(genid) ,payload(payload_) 
            {
                time_stamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
                thread_id = std::this_thread::get_id();
            }

        };

        class Profiler
        {
        
        private:
            std::ofstream log_file;
            std::string log_file_name;
            std::atomic<bool> trace_enabled;
            size_t ring_buf_size;
            mpmcQueueBounded<Event> ring_buf;
            std::thread flusher_thread;


            Profiler(): trace_enabled{false},ring_buf_size{10000},ring_buf{ring_buf_size}
            {
                //ring_buf = mpmcQueueBounded<Event>(ring_buf_size);
                std::filesystem::path logdir = "log";
                try{
                    std::filesystem::create_directory(logdir);
                }
                catch(exception e)
                {
                    Logger::log(Logger::Level::Error, "Profiler",e.what());
                }
                
                auto timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
                log_file_name = "log/actor_trace_"+ std::to_string(timestamp)+".csv";
                
                log_file.open(log_file_name,std::ios::out);
                log_file <<"timestamp,actor_id,gen_id,thread_id,eventType\n";
            }

            void event_flusher()
            {
                while(Profile::Profiler::instance().trace_enabled.load(std::memory_order_acquire))
                {
                    dump_to_csv();
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }

            void dump_to_csv()
            {
                Event ev;
                while(ring_buf.try_pop(ev))
                    log_file << ev.time_stamp<<","<<ev.actor_id<<","<< ev.gen_id<<","<<
                            ev.thread_id << ","<< evtToStr(ev.type) <<"\n"  ;

                return;
            }

        public:
            inline static Profiler& instance()
            {
                static Profiler prof;
                return prof;
            }

            void enableTrace()
            {
                if (!trace_enabled.exchange(true, std::memory_order_relaxed))
                    flusher_thread = std::thread([this](){event_flusher();});
            }

            void disableTrace()
            {
                trace_enabled.store(false,std::memory_order_relaxed);
            }

            template<typename... Args>
            inline void record(Args&&... args)
            {
                if (Profile::Profiler::instance().trace_enabled.load(std::memory_order_relaxed))
                    ring_buf.try_emplace(std::forward<Args>(args)...);
            }

            ~Profiler()
            {
                disableTrace();
                if (flusher_thread.joinable())
                    flusher_thread.join();

                dump_to_csv();
                log_file.close();
            }
        };
    };
}

#endif /* ACTOR_MODEL_LOGGER_TRACER */