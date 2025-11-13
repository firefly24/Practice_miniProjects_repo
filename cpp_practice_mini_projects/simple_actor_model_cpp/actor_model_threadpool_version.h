#ifndef ACTOR_MODEL_THREADPOOL_VERSION_H
#define ACTOR_MODEL_THREADPOOL_VERSION_H

#include <cstddef>
#include <thread>
#include <functional>
#include <string>
#include <memory>
#include <vector>
#include <map>
#include <shared_mutex>
#include <condition_variable>
#include "../simple_mpmc_queue/mpmc_queue_bounded.h"
#include "../simple_ThreadPool/simple_thread_pool.h"
#include "actor_model_logger_tracer.h"

using namespace ActorModel;
using namespace ActorModel::Logger;

using pprof = ActorModel::Profile::Profiler ;

#define NUM_WORKER_THREADS 10

template <typename Task> class Actor;
template <typename Task> class ActorSystem;


enum class ActorState : size_t {
    CREATED,
    RUNNING,
    STOPPING,
    STOPPED,
    FAILED,
    MAX_STATE
};

enum class RecoveryMechanism : size_t {
    RESTART,
    STOP,
    REPLACE,
    IGNORE,
    MAX_MECHANISM
};

struct ActorHandle
{
    size_t idx;
    std::string name;
    ActorHandle(): idx(0), name("") {}
    ActorHandle(size_t id, const std::string& actor_name ): idx(id),name(actor_name) {}
};

struct ActorParameters
{
    size_t mailbox_size;
    size_t idx;
    std::string name;

    ActorParameters(){}

    ActorParameters(size_t mailbox_size_, size_t idx_, std::string name_ = "") : 
                    mailbox_size(mailbox_size_),idx(idx_), name(name_)  {}
};

// RAII style mailbox counter decrement to ensure mailbox msg count is decremented with every pop
/*
struct MailCounterGuard{
    std::atomic<size_t>& counter;

    MailCounterGuard(std::atomic<size_t>& ctr):counter(ctr){}
    
    MailCounterGuard(const MailCounterGuard&) = delete;
    MailCounterGuard& operator=(const MailCounterGuard&) = delete; 

    ~MailCounterGuard()
    {
        counter.fetch_sub(1,std::memory_order_acq_rel);
    }
};
*/

template <typename Task>
struct Message {
    Task task;
    std::string sender_name;
    bool request_reply;
    std::chrono::time_point<std::chrono::steady_clock> timestamp;

    Message(){}

    explicit Message(Task&& t, std::string& sender_m, bool needs_ack = false) 
        :task(std::move(t)), sender_name(sender_m), request_reply(needs_ack)
    {
        //Assuming msg sent by unknown sender is admin msg by ActorSystem itself
        if(sender_name == "")
            sender_name="ADMIN";        
        timestamp = std::chrono::steady_clock::now();
    }

    // Delete copy constructor for msg to make it move only type
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;

    // Default move constructors
    Message(Message&&) = default;
    Message& operator=(Message&&) = default;
    
};

template <typename Task>
struct ActorSlot
{
    std::atomic<bool> is_valid;
    std::atomic<uint64_t> gen_id;
    std::unique_ptr<Actor<Task>> actor;


    ActorSlot(): is_valid{false}, actor(nullptr){}
    ActorSlot(std::unique_ptr<Actor<Task>> &&actor_): is_valid{true}, gen_id{0},actor(std::move(actor_)) {}

};

template <typename Task>
class Actor
{
private:
    std::unique_ptr<mpmcQueueBounded<Message<Task>>> mailbox_q;  //Actor mailbox, using lock-free mpmc queue (only one consumer being self)
    size_t mailbox_size_;
    std::atomic<size_t> mailbox_count_;
    std::atomic<bool> actor_alive_; // flag to track if actor is alive to receive msgs
    std::weak_ptr<ActorSystem<Task>> owning_system_;
    std::atomic<ActorState> actor_state_;  // Records current state of the actor

    // Invoke if sender requests a reply, so send a successful acknowledgement to sender mailbox
    /*
    void handleReply(const Message<Task>& msg, bool is_success=true)
    {
        if (auto actor_system = owning_system_.lock() )
        {
            if(actor_system->getActor(msg.sender_name))
                send(msg.sender_name,[this,is_success](){std::cout << "Message handled by Actor "<<this->name_ << ": " 
                                    << (is_success?"Successfully":"Failed") << std::endl;});
        }
    } 
    */

public:
    size_t id_; // Keeping a actor id to identify an actor, will decide in future how to use this
    std::string name_; //Have an actor name, to get pretty logs!
    RecoveryMechanism recovery_strategy_;   // Add recovery strategy if actor stops
    std::atomic<bool> is_draining_;
    std::atomic<uint64_t> gen_id_;

    explicit Actor(size_t mailbox_size, size_t id,std::string name="",uint64_t gen_id=0)
        :mailbox_size_(mailbox_size),mailbox_count_(0),actor_alive_(true),actor_state_(ActorState::CREATED),
        id_(id),name_(name),is_draining_(false), gen_id_(gen_id)
    {
        recovery_strategy_ = RecoveryMechanism::RESTART;
        mailbox_q = std::make_unique<mpmcQueueBounded<Message<Task>>>(mailbox_size);
        actor_state_.store(ActorState::RUNNING,std::memory_order_release); 
    }

    // We want our actor to keep a reference to the owning actor system that spawned it, 
    //so that we can use it to communicate important info to the owning system later
    void setActorSystem(std::shared_ptr<ActorSystem<Task>> actor_system)
    {
        owning_system_ = std::weak_ptr<ActorSystem<Task>>(actor_system);
    }

    std::shared_ptr<ActorSystem<Task>> getActorSystem()
    {
        return owning_system_.lock();
    }

    // API to check if actor is active or stopped
    bool isAlive()
    {
        return actor_alive_.load(std::memory_order_acquire);
    }

    bool isFailedState()
    {
        return (actor_state_.load(std::memory_order_acquire) == ActorState::FAILED);
    }

    void getActorProperties(ActorParameters &actor_parameters)
    {
        actor_parameters.mailbox_size = mailbox_size_;
        actor_parameters.idx = id_;
        actor_parameters.name = name_;
    }

    // Push a task to this actor's mailbox
    bool addToMailbox(Message<Task>&& msg)
    {
        //size_t retry_loop=0;
        //once actor is stopped, but some thread keeps pushing successfully back to back, we won't enter while loop
        if (!actor_alive_.load(std::memory_order_acquire))
            return false;

        if (!mailbox_q->try_push(std::move(msg)) )
        {
            //if ((++retry_loop > 10) )
            //std::this_thread::yield();
            pprof::instance().record(ActorModel::Profile::EventType::Fail,id_,gen_id_, 1234);
            
            //try_push may fail due to mailbox full, trigger a drain in that case
            bool expected_draining = false;
            if (is_draining_.compare_exchange_strong(expected_draining,true,std::memory_order_acq_rel))
                if (auto actor_system = owning_system_.lock())
                    actor_system->notifyMailboxActive(id_);
            return false;

        }
        pprof::instance().record(ActorModel::Profile::EventType::Enqueue,id_,gen_id_, 1234);

        bool expected_draining = false;
        if (is_draining_.compare_exchange_strong(expected_draining,true,std::memory_order_acq_rel))
            if (auto actor_system = owning_system_.lock())
                actor_system->notifyMailboxActive(id_);

        return true;
    }

    // API to send a task from this actor to receiver actor
    bool send(const std::string& receiver,Task&& task,bool needs_ack = false)
    {
        if(actor_alive_.load(std::memory_order_acquire) )
        {
            if (auto actor_system = owning_system_.lock())
                return actor_system->send(receiver,Message<Task>{std::move(task),name_,needs_ack});
        }
        return false;
    }

    void handleMsg(Message<Task>&& msg)
    {
        //std::cout << name_ << ": "; 
        try
        {
            pprof::instance().record(ActorModel::Profile::EventType::Dequeue,id_,gen_id_, 1234);
            msg.task();   // Execute task            
        }
        catch(const std::exception& e)
        {
            if (actor_alive_.exchange(false,std::memory_order_acq_rel) && 
                (actor_state_.load(std::memory_order_acquire) != ActorState::FAILED))
            {
                actor_state_.store(ActorState::FAILED,std::memory_order_release);
                //if (actor_state_.compare_exchange_strong())

                if(auto actor_system = owning_system_.lock())
                {
                    actor_system->notifyActorFailure(id_);
                    actor_system->logFailure(name_,std::move(msg));
                }
            }
            //std::cout <<name_ << ": Exception caught while draining: " << e.what()<<  std::endl;
            Logger::log(Level::Error, name_, e.what());
            return;
        }
        //if (msg.request_reply)
         //       handleReply(msg,true);
    }

    // Once actor is stopped, flushes out mailbox
    void drainMailbox()
    {
        // Flush out all tasks remaining in the queue by executing them
        pprof::instance().record(ActorModel::Profile::EventType::DrainStart,id_,gen_id_, 1234);
        Message<Task> remaining_msg;
        while(actor_alive_.load(std::memory_order_acquire) && mailbox_q->try_pop(remaining_msg))
            handleMsg(std::move(remaining_msg));

        is_draining_.store(false,std::memory_order_release);

        if ((actor_alive_.load(std::memory_order_acquire)) && mailbox_q->try_pop(remaining_msg))
        {
            bool expected_draining = false;
            if (is_draining_.compare_exchange_strong(expected_draining,true))
                if (auto actor_system = owning_system_.lock())
                    actor_system->notifyMailboxActive(id_);
            handleMsg(std::move(remaining_msg));
        }
        pprof::instance().record(ActorModel::Profile::EventType::DrainEnd,id_,gen_id_, 1234);

        // Some msgs might get queued just before the exit, but for fairness I don't want to keep draining
        // So the new msgs will stay idle in the actor mailbox till they are picked up my a worker thread again
    }

    // Stop the mailbox checker thread
    void stopActor()
    {
        //if (actor_alive_.exchange(false,std::memory_order_acq_rel) || isFailedState())
        {
            actor_alive_.store(false,std::memory_order_release);
            //std::cout << "Stopping Actor: " << name_ << std::endl;
            Logger::log(Level::Info, name_, "Stopping Actor");
            while(is_draining_.load(std::memory_order_acquire))
            { 
                std::this_thread::yield();
            }
            actor_state_.store(ActorState::STOPPED,std::memory_order_release);
        }
    }

    // Destructor
    ~Actor()
    {
        //std::cout << name_ << ": Destrutor Called" << std::endl;

        ActorState expected = actor_state_.load(std::memory_order_acquire);
        if (expected != ActorState::STOPPED)
            stopActor();
    }

    //delete copy constructors
    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    //set move constuctors to default;
    Actor(Actor&&) = default;
    Actor& operator=(Actor &&) = default;

};

// Packages a function and its arguments into a "Task" type, which is compatible to push in actor mailboxes
template <typename Task, typename Func, typename... Args>
Task constructTask(Func&& func, Args&&... args)
{
    Task task = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
    return task;
}


// ActorSystems manage the lifecyle of group of Actors
template <typename Task>
class ActorSystem : public std::enable_shared_from_this<ActorSystem<Task>>
{
private:
    std::atomic<size_t> active_actors_; // Tracks current active actors in the system
    size_t total_actors_;   // The max no. of actors this ActorSystem can manage
    std::vector<ActorSlot<Task>> actor_slots_;
    std::shared_mutex registry_lock_;  // lock to handle concurrent registers and unregisters for actors
    std::unordered_map<std::string,size_t> actor_registry_; //maintain a actor registry to easily refer an actor by name
    std::condition_variable actors_pending_cleanup_;  // condition variable to control access to cleanup queue
    std::queue<size_t> binned_actor_idx;  // cleanup queue storing idx of actors to destroy
    std::thread cleanup_thread_; // Thread that keeps polling to pop from cleanup queue to destroy failed actors
    std::mutex cleanup_mtx_;    // Mutex to control access to cleanup queue

public:
    ThreadPool_Q worker_pool_;

    ActorSystem(size_t numActors):
                active_actors_(0), total_actors_(numActors),
                worker_pool_(numActors*4, NUM_WORKER_THREADS)
    {
        pprof::instance();
        pprof::instance().enableTrace();
        actor_slots_ = std::vector<ActorSlot<Task>>(numActors);
        cleanup_thread_ = std::thread([this](){cleanup_actors();});
    }

    // Add a newly spawned actor into our registry
    //bool registerActor(size_t& actor_id)
    bool registerActor(size_t& requested_id,std::string& requested_name,size_t mailbox_capacity=1)
    {
        std::unique_lock<std::shared_mutex> wr_lock(registry_lock_);

        if(actor_slots_[requested_id].actor
            || (actor_registry_.find(requested_name) != actor_registry_.end()))
        {
            //std::cout << "Actor id: " << requested_name << "already holds active actor" << std::endl;
            Logger::log(Level::Warn, "ActorSystem", requested_name + " is taken"  );
            return false;
        }
        actor_slots_[requested_id].actor = std::make_unique<Actor<Task>>(mailbox_capacity,requested_id,requested_name,actor_slots_[requested_id].gen_id);
        actor_slots_[requested_id].is_valid.store(true,std::memory_order_release);
        actor_slots_[requested_id].actor->setActorSystem(this->shared_from_this());

        actor_registry_.emplace(requested_name,requested_id);

        Logger::log(Level::Info, requested_name, "Successfully registered ");
        pprof::instance().record(ActorModel::Profile::EventType::Register, requested_id,actor_slots_[requested_id].gen_id, 1234);

        return true;
    }

    // Get the actor pointer by name from our registry, just like a phonebook

    // Spawn a new actor and add register it in the system registry
    ActorHandle spawn(size_t mailbox_capacity=1,std::string name="",int idx=-1)
    {
        if (name == "")
        {
            //std::cout << "Cannot create nameless Actor" << std::endl;
            Logger::log(Level::Warn, "ActorSystem", "Cannot create nameless Actor");
            return {};
        }
        while(1)
        {
            // when we want to spawn an actor at a specific index, 
            //specially when an already existing actor fails and stops, and as part of recovery mechanism, we respawn same actor
            // I will use this spawn with idx later, for adding recovery mechanisms
            if ((idx != -1) && 
                (idx >= 0 && (size_t)idx< total_actors_ ) ) // if idx is in valid range
            {
                size_t idx_ = (size_t)idx;
                if ( registerActor(idx_,name,mailbox_capacity) )
                {

                    return ActorHandle(idx_,actor_slots_[idx_].actor->name_);
                }
                
                //std::cout << "Actor creation failed for id: " << idx << std::endl;
                Logger::log(Level::Warn, "ActorSystem", "Actor creation failed for id: " + to_string(idx));
                return {};
            }

            // Actor System has reached max capacity, cannot add and spawn new actors 
            size_t available_slot_idx = active_actors_.load(std::memory_order_acquire);
            if (available_slot_idx >= total_actors_)
                return {};

            // method to spawn completely new actor for the first time, and not used for respawning failed actor
            if (active_actors_.compare_exchange_strong(available_slot_idx,available_slot_idx+1, std::memory_order_acq_rel))
            {
                //std::shared_ptr<Actor<Task>> new_actor= std::make_shared<Actor<Task>>(mailbox_capacity,curr_size,name);

                if ( registerActor(available_slot_idx,name,mailbox_capacity) )
                    return ActorHandle(available_slot_idx,actor_slots_[available_slot_idx].actor->name_);

                // register actor failed, rollback active actors
                active_actors_.fetch_sub(1,std::memory_order_release);
                /* in case where multiple threads are calling spawn on same actor system, I see this rollback mechanism might fail.
                    Because by the time this rollback hits, another thread might have spawned a new actor and incremented the index.
                    this rollback will render that slot invalid, and waste the current slot also 
                    So for now, I am assuming that only a single thread can call spawn on an actor system, and actors are spawned sequentially
                    -(can this be enforced by using unique ptr for ActorSystem ?)
                    TODO: How will this work in multithreaded spawn calls ?
                    */
                return {};
            }
            std::this_thread::yield();
        }
        return {};
    }

    //void notifyMailboxActive(std::weak_ptr<Actor<Task>> weak_actor)
    void notifyMailboxActive(size_t actor_id)
    {
        //std::cout << "notifyMailboxActive invoked" << std::endl;

        bool push_done = worker_pool_.tryPush([this,actor_id]() { if (this->actor_slots_[actor_id].actor 
                                                    && this->actor_slots_[actor_id].is_valid.load(std::memory_order_acquire)
                                                    )
                                                        this->actor_slots_[actor_id].actor->drainMailbox();
                                            });

        // Log failure for threadpool push fail                                
        if (!push_done)
            pprof::instance().record(ActorModel::Profile::EventType::Fail, actor_id,actor_slots_[actor_id].gen_id,1234);
                                            
    }

    // Helper function to send msg based on actor name instead of pointers, from sender -> receiver actor

    // send() by receiver id
    bool send(size_t receiver_id, Message<Task>&& msg)
    {

        if (!actor_slots_[receiver_id].is_valid.load(std::memory_order_relaxed))
            return false;

        // Acquire the actor raw pointer for send
        Actor<Task>* receiver = actor_slots_[receiver_id].actor.get();
        if(!receiver)
            return false;

        if (actor_slots_[receiver_id].is_valid.load(std::memory_order_acquire))
            return receiver->addToMailbox(std::move(msg));
        return false;
    }

    // send() by receiver name
    bool send(const std::string& receiver_name, Message<Task>&& msg)
    {   
        std::shared_lock<std::shared_mutex> rlock(registry_lock_);
        if (!actor_registry_.contains(receiver_name))
            return false;
        return send(actor_registry_[receiver_name],std::move(msg));
    }

    // Keeping this api , when I want to send nested tasks, so task wrapping in msg will be on the fly
    bool send(const std::string& sender_name,const std::string& receiver_name, Task&& task,bool needs_ack)
    {   
        std::shared_lock<std::shared_mutex> rlock(registry_lock_);
        auto it = actor_registry_.find(sender_name);
        if ( (it != actor_registry_.end()) && actor_slots_[it->second].actor)
        {
            rlock.unlock();
            if(actor_slots_[it->second].is_valid.load(std::memory_order_acquire))
                return actor_slots_[it->second].actor->send(receiver_name,std::move(task),needs_ack);
        }
        return false;
    }

    // will be called when ActorSystem wants to send admin tasks to an actor directly
    bool send(const std::string& receiver_name, Task&& task)
    {   
        std::shared_lock<std::shared_mutex> rlock(registry_lock_);
        auto it = actor_registry_.find(receiver_name);
        if ( (it != actor_registry_.end()) && actor_slots_[it->second].actor)
        {
            rlock.unlock();
            std::string admin = "";
            if(actor_slots_[it->second].is_valid.load(std::memory_order_acquire))
                return actor_slots_[it->second].actor->addToMailbox(Message<Task>{std::move(task),admin,false});
        }
        return false;
    }

    void cleanup_actors()
    {
        size_t cleanup_idx;

        while(1)
        {
            std::unique_lock<std::mutex> cleanup_lock(cleanup_mtx_);
            actors_pending_cleanup_.wait(cleanup_lock,[this](){return !binned_actor_idx.empty(); });
            
            cleanup_idx = binned_actor_idx.front();
            binned_actor_idx.pop();
            cleanup_lock.unlock();
            
            if ((size_t)cleanup_idx == total_actors_) // Sentinel to stop cleanup thread
                    return;

            if (actor_slots_[cleanup_idx].actor 
                && !actor_slots_[cleanup_idx].is_valid.load(std::memory_order_acquire))
            {
                if (actor_slots_[cleanup_idx].actor->recovery_strategy_ == RecoveryMechanism::RESTART )
                {
                    ActorParameters failed_actor_params;
                    actor_slots_[cleanup_idx].actor->getActorProperties(failed_actor_params);
                    // Since only failed actors come to this path, we have already set actor_alive_ as false
                    actor_slots_[cleanup_idx].gen_id.fetch_add(1,std::memory_order_relaxed);
                    unregisterActor(cleanup_idx);
                    
                    auto renewed_actor = spawn(failed_actor_params.mailbox_size,failed_actor_params.name,cleanup_idx);
                    pprof::instance().record(ActorModel::Profile::EventType::Restart, cleanup_idx,actor_slots_[cleanup_idx].gen_id,1234);
                    continue;
                }
            }
            unregisterActor(cleanup_idx);
        }
    }

    // If actor throws exception while handling a task, 
    //first we need to stop the actor from accepting new msgs, and then log this failure
    //void notifyActorFailure(std::shared_ptr<Actor<Task>> actor)
    void notifyActorFailure(size_t actor_id)
    {
        std::unique_lock<std::mutex> cleanup_lock(cleanup_mtx_);
        /*ACTOR SLOT*/
        actor_slots_[actor_id].is_valid.store(false,std::memory_order_release);
        binned_actor_idx.push(actor_id);
        cleanup_lock.unlock();
        actors_pending_cleanup_.notify_one();
    }

    // Just logs the failure if an exception occurs when an actor is executing a task
    void logFailure(const std::string& failed_actor,Message<Task>&& msg)
    {
        //std::cout << "Terminating Actor: "<< failed_actor << " due to execption" << std::endl;
        Logger::log(Level::Error, failed_actor, "Terminating Actor due to execption " );
        if(msg.request_reply)
        {
            if (!actor_registry_.contains(msg.sender_name))
                return ;
            send(msg.sender_name,
                        [failed_actor](){std::cout << "Task failed with exception by Actor: "<< failed_actor << std::endl;});
        }
    }

    // If actor at given idx exists, remove from the actor_pool_ and destroy the associated actor object
    void unregisterActor(int idx)
    {
        std::unique_lock<std::shared_mutex> wrlock(registry_lock_);

        if (actor_slots_[idx].actor)
        {
            actor_registry_.erase(actor_slots_[idx].actor->name_);
            wrlock.unlock();
            actor_slots_[idx].actor->stopActor();
            //std::cout << "Unregistering actor: " << actor_slots_[idx].actor->name_ <<  ":  " << actor_slots_[idx].actor.get() <<std::endl;
            Logger::log(Level::Info, actor_slots_[idx].actor->name_, "Unregistering Actor" );
            pprof::instance().record(ActorModel::Profile::EventType::Unregister,idx,actor_slots_[idx].gen_id, 1234);
            actor_slots_[idx].is_valid.store(false,std::memory_order_release);    
            actor_slots_[idx].actor.reset();
            //else
            //    std::cout << "Failed to destry actor!" << std::endl;
        }
    }

    ~ActorSystem()
    {
        pprof::instance().record(ActorModel::Profile::EventType::StopSystem,0,0, 1234);
        worker_pool_.stopPool();
        if (cleanup_thread_.joinable())
        {
            {
                std::unique_lock<std::mutex> cleanup_lock(cleanup_mtx_);
                binned_actor_idx.push(total_actors_);
            }
            actors_pending_cleanup_.notify_all();
            cleanup_thread_.join();
        }

        for(size_t i=0;i<total_actors_;i++)
            unregisterActor(i);
    }
};

#endif /* ACTOR_MODEL_THREADPOOL_VERISON_H */