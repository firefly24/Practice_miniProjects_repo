#include <cstddef>
#include <thread>
#include <functional>
#include <semaphore>
#include <string>
#include <memory>
#include <vector>
#include <map>
#include <iostream>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include "../simple_mpmc_queue/mpmc_queue_bounded.h"

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

template <typename Task>
struct Message{
    Task task;
    std::weak_ptr<Actor<Task>> sender;
    std::string sender_handle;
    bool request_reply;
    std::chrono::time_point<std::chrono::steady_clock> timestamp;

    Message(){}
    explicit Message(Task&& t, std::shared_ptr<Actor<Task>> sender_m, bool needs_ack = false) 
        :task(std::move(t)), sender(std::weak_ptr<Actor<Task>>(sender_m)), request_reply(needs_ack)
    {
        if(sender_m)
            sender_handle = sender_m->name_;
        else
            sender_handle="ADMIN";      //Assuming msg sent by unknown sender is admin msg by ActorSystem itself
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
class Actor: public std::enable_shared_from_this<Actor<Task>>
{
private:
    std::unique_ptr<mpmcQueueBounded<Message<Task>>> mailbox_q;  //Actor mailbox, using lock-free mpmc queue (only one consumer being self)
    std::atomic<bool> actor_alive_; // flag to track if actor is alive to receive msgs
    std::thread dispatcher_;    // dispatcher thead that keeps checking mailbox for new tasks to execute
    std::counting_semaphore<> new_mail_arrived_{0}; // use counting semaphore to keep track of tasks in the maibox waiting to be picked
    std::weak_ptr<ActorSystem<Task>> owning_system_;
    std::atomic<ActorState> actor_state_;  // Records current state of the actor

    // Keep polling mailbox for new tasks
    void checkMailbox()
    {
        while(actor_alive_.load(std::memory_order_acquire))
        {
            new_mail_arrived_.acquire();    // wait for semaphore to be signalled when new mail arrives
            receive();                      // Pop the msg from mailbox to execute task
        }

        // If current actor state is RUNNING, only then drain mailbox, if state is FAILED, we don't want to drain
        ActorState expected_running_state = ActorState::RUNNING;
        if (actor_state_.compare_exchange_strong(expected_running_state,ActorState::STOPPING))
            drainMailbox();

        if(isFailedState())     // In failed state, let us drop the remaining msgs from mailbox, and notify actorSystem of failure
            if (auto actor_system = owning_system_.lock())
                actor_system->notifyActorFailure(this->shared_from_this());
        
        std::cout <<name_ <<": Stopped dispatcher thread" <<std::endl;
    }

    // Open a msg from mailbox and execute the task
    void receive()
    {
        Message<Task> msg;
        if(mailbox_q->try_pop(msg))
        {
            std::cout << name_ << ": "; 
            try
            {
                msg.task(); // Executes task
                
                // Since our task returns void, sending success  only means task is successfully enqueued at receiver end
                if (msg.request_reply)
                    handleReply(msg,true);
            }
            catch(...)
            {
                std::cout << "Exception caught in receive()" << std::endl;
                actor_alive_.store(false,std::memory_order_release);
                actor_state_.store(ActorState::FAILED,std::memory_order_release);

                if(auto actor_system = owning_system_.lock())
                {
                    actor_system->logFailure(this->name_,std::move(msg));
                }
            }
        }
    }

    // Once actor is stopped, flushes out mailbox
    void drainMailbox()
    {
        //std::cout << "Entering Drain mailbox" << std::endl;
        // Flush out all tasks remaining in the queue by executing them
        Message<Task> remaining_msg;
        while(mailbox_q->try_pop(remaining_msg))
        {
            std::cout << name_ << ": "; 
            try
            {
                remaining_msg.task();   // Execute task

                // Since our task returns void, sending success  only means task is successfully enqueued at receiver end
                if (remaining_msg.request_reply)
                    handleReply(remaining_msg,true);
            }
            catch(...)
            {
                //std::cout << "Exception Caught in DrainMailbox" <<std::endl;
                if(auto actor_system = owning_system_.lock())
                    actor_system->logFailure(this->name_,std::move(remaining_msg));
                return;
            }
        }
    }

    // Invoke if sender requests a reply, so send a successful acknowledgement to sender mailbox
    void handleReply(const Message<Task>& msg, bool is_success=true)
    {
        if (auto sender = msg.sender.lock())
            send(sender,[this,is_success](){std::cout << "Message handled by Actor "<<this->name_ << ": " 
                                    << (is_success?"Successfully":"Failed") << std::endl;});
    }

public:
    size_t id_; // Keeping a actor id to identify an actor, will decide in future how to use this
    std::string name_; //Have an actor name, to get pretty logs!

    Actor(size_t mailbox_size, size_t id):actor_alive_(true),actor_state_(ActorState::CREATED),id_(id)
    {
        name_ ="";
        mailbox_q = std::make_unique<mpmcQueueBounded<Message<Task>>>(mailbox_size);
        dispatcher_ = std::thread([this](){
                                            this->actor_state_.store(ActorState::RUNNING,std::memory_order_release);
                                            checkMailbox();
                                        });  //Start check mailbox loop 
    }

    Actor(size_t mailbox_size, size_t id,std::string name)
        :actor_alive_(true),actor_state_(ActorState::CREATED),id_(id),name_(name)
    {
        mailbox_q = std::make_unique<mpmcQueueBounded<Message<Task>>>(mailbox_size);
        actor_state_.store(ActorState::RUNNING,std::memory_order_release);
        dispatcher_ = std::thread([this](){
                                            this->actor_state_.store(ActorState::RUNNING,std::memory_order_release);
                                            checkMailbox();
                                        });                  
    }

    // We want our actor to keep a reference to the owning actor system that spawned it, 
    //so that we can use it to communicate important info to the owning system later
    void setActorSystem(std::shared_ptr<ActorSystem<Task>> actor_system)
    {
        owning_system_ = std::weak_ptr<ActorSystem<Task>>(actor_system);
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

    // Push a task to this actor's mailbox
    bool addToMailbox(Message<Task>&& msg)
    {
        //once actor is stopped, but some thread keeps pushing successfully back to back, we won't enter while loop
        if (!actor_alive_.load(std::memory_order_acquire))
            return false;

        size_t retry_count=0;
        while (!mailbox_q->try_push(std::move(msg)))
        {
            if (!actor_alive_.load(std::memory_order_acquire))     // Actor is dead, don't push any tasks anymore
                return false; 

            retry_count++;
            std::this_thread::yield();
            if(retry_count >16)
                return false;
        }   
        new_mail_arrived_.release();        // Signal new mail has arrived 
        return true;
    }

    // API to send a task from this actor to receiver actor
    bool send(std::shared_ptr<Actor<Task>> receiver,Task&& task,bool needs_ack = false)
    {
        if(actor_alive_.load(std::memory_order_acquire) && receiver )
            return receiver->addToMailbox(Message<Task>{std::move(task),this->shared_from_this(),needs_ack});
        return false;
    }

    void recoveryMechanism()
    {
        std::cout << "Actor's recovery mechanism" << std::endl;
    }

    // Stop the mailbox checker thread
    void stopActor()
    {
        if (actor_alive_.exchange(false,std::memory_order_acq_rel) || isFailedState())
        {
            std::cout << "Stopping Actor: " << name_ << std::endl;
            new_mail_arrived_.release();
            if(dispatcher_.joinable())
                dispatcher_.join();
            actor_state_.store(ActorState::STOPPED,std::memory_order_release);
        }
    }

    // Destructor
    ~Actor()
    {
        std::cout << name_ << ": Destrutor Called" << std::endl;
        stopActor();
    }

    //delete copy constructors
    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    //set move constuctors to delete;
    Actor(Actor&&) = delete;
    Actor& operator=(Actor &&) = delete;

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
    std::mutex registry_lock_;  // lock to handle concurrent registers and unregisters for actors
    std::unordered_map<std::string,std::weak_ptr<Actor<Task>>> actor_registry_; //maintain a actor registry to easily refer an actor by name
    std::condition_variable actors_pending_cleanup_;  // condition variable to control access to cleanup queue
    std::queue<size_t> binned_actor_idx;  // cleanup queue storing idx of actors to destroy
    std::thread cleanup_thread_; // Thread that keeps polling to pop from cleanup queue to destroy failed actors
    std::mutex cleanup_mtx_;    // Mutex to control access to cleanup queue

public:

    std::vector<std::shared_ptr<Actor<Task>>> actor_pool_;

    ActorSystem(size_t numActors):   active_actors_(0), total_actors_(numActors) {
        actor_pool_=std::vector<std::shared_ptr<Actor<Task>>>(numActors) ;
        cleanup_thread_ = std::thread([this](){cleanup_actors();});
    }

    // Add a newly spawned actor into our registry
    bool registerActor(std::shared_ptr<Actor<Task>> actor)
    {
        // Do not allow actor with no name
        if (actor->name_ == "")
            return false;

        std::lock_guard<std::mutex> rlock(registry_lock_);
        if((actor_registry_.find(actor->name_) != actor_registry_.end()) && actor_registry_[actor->name_].lock())
        {
            std::cout << "Name: "<<actor->name_<<" is taken" << std::endl;
            return false;
        }
        actor_registry_[actor->name_] = std::weak_ptr<Actor<Task>>(actor);
        return true;
    }

    // Get the actor pointer by name from our registry, just like a phonebook
    std::shared_ptr<Actor<Task>> getActor(const std::string& actor_name)
    {
        std::lock_guard<std::mutex> rlock(registry_lock_);
        if(actor_registry_.find(actor_name) == actor_registry_.end())
            return {};
        return actor_registry_[actor_name].lock();
    }

    // Spawn a new actor and add register it in the system registry
    std::shared_ptr<Actor<Task>> spawn(size_t mailbox_capacity=1,std::string name="",int idx=-1)
    {
        size_t curr_size;
        while(1)
        {
            // when we want to spawn an actor at a specific index, 
            //specially when an already existing actor fails and stops, and as part of recovery mechanism, we respawn same actor
            // I will use this spawn with idx later, for adding recovery mechanisms
            if (idx != -1) // if idx is in valid range
            {
                if (idx >= 0 && (size_t)idx< total_actors_ )
                {
                    std::shared_ptr<Actor<Task>> new_actor= std::make_shared<Actor<Task>>(mailbox_capacity,curr_size,name);

                    if ( registerActor(new_actor) )
                    {
                        new_actor->setActorSystem(this->shared_from_this());
                        actor_pool_[idx] = new_actor;
                        return new_actor;
                    }
                }
            }

            // Actor System has reached max capacity, cannot add and spawn new actors 
            curr_size = active_actors_.load(std::memory_order_relaxed);
            if (curr_size >= total_actors_)
                return {};

            // method to spawn completely new actor for the first time, and not used for respawning failed actor
            if (active_actors_.compare_exchange_strong(curr_size,curr_size+1, std::memory_order_release,std::memory_order_acquire))
            {
                std::shared_ptr<Actor<Task>> new_actor= std::make_shared<Actor<Task>>(mailbox_capacity,curr_size,name);

                if ( registerActor(new_actor) )
                {
                    new_actor->setActorSystem(this->shared_from_this());
                    actor_pool_[curr_size] = new_actor;
                    return actor_pool_[curr_size];
                }
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

    // Helper function to send msg based on actor name instead of pointers, from sender -> receiver actor
    bool send(const std::string& sender_handle,const std::string& receiver_handle, Task&& task, bool needs_ack=false)
    {   
        auto receiver = getActor(receiver_handle);
        auto sender = getActor(sender_handle);
        if (!receiver || !sender)
            return false;        

        return sender->send(receiver,std::move(task),needs_ack);
    }

    // Helper function overload, directly using actor pointers
    bool send(std::shared_ptr<Actor<Task>> sender,std::shared_ptr<Actor<Task>> receiver,Task&& task,bool needs_ack=false)
    {
        if(!receiver || !sender)
            return false;
        return sender->send(receiver,std::move(task),needs_ack);
    }

    // Send function to send any admin type messages from the ActorSystem to Actor
    bool send(const std::string& receiver_handle, Task&& task)
    {   
        auto receiver = getActor(receiver_handle);
        if (!receiver)
            return false;

        if ( !receiver->addToMailbox(Message<Task>{std::move(task),{},false}) )
        {
            std::cout << receiver->name_ <<":Actor is stopped! Mailbox is closed for new mails!" << std::endl;
            return false;
        }
        return true;
    }

    void cleanup_actors()
    {
        int actor_to_cleanup_idx;

        while(1)
        {
            std::unique_lock<std::mutex> cleanup_lock(cleanup_mtx_);

            actors_pending_cleanup_.wait(cleanup_lock,[this](){return !binned_actor_idx.empty(); });
            
            actor_to_cleanup_idx = binned_actor_idx.front();
            binned_actor_idx.pop();

            cleanup_lock.unlock();
            
            if ((size_t)actor_to_cleanup_idx == total_actors_) // Sentinel to stop cleanup thread
                    return;

            unregisterActor(actor_to_cleanup_idx);
        }
    }

    // If actor throws exception while handling a task, 
    //first we need to stop the actor from accepting new msgs, and then log this failure
    void notifyActorFailure(std::shared_ptr<Actor<Task>> actor)
    {
        std::unique_lock<std::mutex> cleanup_lock(cleanup_mtx_);
        binned_actor_idx.push(actor->id_);
        cleanup_lock.unlock();
        actors_pending_cleanup_.notify_one();
    }

    // Just logs the failure if an exception occurs when an actor is executing a task
    void logFailure(std::string actor_name,Message<Task>&& msg)
    {
        std::cout << "Terminating Actor: "<< actor_name<< " due to execption" << std::endl;
        if(msg.request_reply)
        {
            if (auto sender = msg.sender.lock())
                send(sender->name_,[actor_name](){std::cout << "Task failed with exception by Actor: "<< actor_name << std::endl;});
        }
    }

    // Stop the actor and unregister from registry 
    void notifyActorStopped(int actor_id)
    {
        unregisterActor(actor_id);
    }

    void recoveryPolicy(std::shared_ptr<Actor<Task>> actor)
    {
        std::cout <<"Add recovery policy here for Actor: " << actor->name_ << std::endl;
        actor->recoveryMechanism();
    }

    // If actor at given idx exists, remove from the actor_pool_ and destroy the associated actor object
    void unregisterActor(int idx)
    {
        std::lock_guard<std::mutex> rlock(registry_lock_);
        if (actor_pool_[idx])
        {
            std::cout << "Unregistering actor: " << actor_pool_[idx]->name_ << std::endl;
            actor_registry_.erase(actor_pool_[idx]->name_);
            actor_pool_[idx].reset();
        }
    }

    ~ActorSystem()
    {
        {
            std::unique_lock<std::mutex> cleanup_lock(cleanup_mtx_);
            binned_actor_idx.push(total_actors_);
        }
        actors_pending_cleanup_.notify_all();

        if (cleanup_thread_.joinable())
            cleanup_thread_.join();

        for(size_t i=0;i<total_actors_;i++)
            unregisterActor(i);
    }
};