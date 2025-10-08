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
#include "../simple_mpmc_queue/mpmc_queue_bounded.h"

template <typename Task> class Actor;
template <typename Task> class ActorSystem;

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

    // Open a msg from mailbox and execute the task
    void receive()
    {
        Message<Task> msg;
        if(mailbox_q->try_pop(msg))
        {
            std::cout << name_ << ": "; 
            msg.task();     // Executes task
            if (msg.request_reply)
            {
                // Since our task returns void, sending success  only means task is successfully enqueued at receiver end
                handleReply(msg);
            }
        }
    }

    // Invoke if sender requests a reply, so send a successful acknowledgement to sender mailbox
    void handleReply(const Message<Task>& msg)
    {
        auto sender = msg.sender.lock();
        if(sender)
            send(sender,[this](){std::cout << "Message handled successfully by Actor: "<<this->name_ << std::endl;});
    }

    // Once actor is stopped, flushes out mailbox
    void drainMailbox()
    {
        // Flush out all tasks remaining in the queue by executing them
        Message<Task> remaining_msg;
        while(mailbox_q->try_pop(remaining_msg))
        {
            std::cout << name_ << ": "; 
            remaining_msg.task();
            if (remaining_msg.request_reply)
            {
                // Since our task returns void, sending success  only means task is successfully enqueued at receiver end
                handleReply(remaining_msg);
            }
        }
    }

    // Keep polling mailbox for new tasks
    void checkMailbox()
    {
        while(actor_alive_.load(std::memory_order_acquire))
        {
            new_mail_arrived_.acquire();    // wait for semaphore to be signalled when new mail arrives
            receive();                      // Pop the msg from mailbox to execute task
        }

        // Actor is stopped, execute the remaining process in the mailbox
        drainMailbox();
        std::cout <<name_ <<": Stopping dispatcher thread" <<std::endl;
    }

public:
    size_t id_; // Keeping a actor id to identify an actor, will decide in future how to use this
    std::string name_; //Have an actor name, to get pretty logs!

    Actor(size_t mailbox_size, size_t id):actor_alive_(true),id_(id)
    {
        name_ ="";
        mailbox_q = std::make_unique<mpmcQueueBounded<Message<Task>>>(mailbox_size);
        dispatcher_ = std::thread([this](){checkMailbox();});  //Start check mailbox loop 
    }

    Actor(size_t mailbox_size, size_t id,std::string name)
        :actor_alive_(true),id_(id),name_(name)
    {
        mailbox_q = std::make_unique<mpmcQueueBounded<Message<Task>>>(mailbox_size);
        dispatcher_ = std::thread([this](){checkMailbox();});
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
        if(!actor_alive_.load(std::memory_order_acquire))
            return false;
        if (receiver)
            return receiver->addToMailbox(Message<Task>{std::move(task),this->shared_from_this(),needs_ack});
        return false;
    }

    // Stop the mailbox checker thread
    void stopActor()
    {
        //actor_alive_.store(false,std::memory_order_release);
        if (actor_alive_.exchange(false,std::memory_order_acq_rel))
        {
            new_mail_arrived_.release();
            if(dispatcher_.joinable())
                dispatcher_.join();
            
            std::shared_ptr<ActorSystem<Task>> owning_actor_system = owning_system_.lock();
            if(owning_actor_system)
                owning_actor_system->unregisterActor(this->name_);  // remove from registry so that name can be reused later
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

public:

    std::vector<std::shared_ptr<Actor<Task>>> actor_pool_;

    ActorSystem(size_t numActors):   active_actors_(0), total_actors_(numActors) {
        actor_pool_=std::vector<std::shared_ptr<Actor<Task>>>(numActors) ;
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
    std::shared_ptr<Actor<Task>> spawn(size_t mailbox_capacity=1,std::string name="")
    {
        size_t curr_size;
        while(1)
        {
            curr_size = active_actors_.load(std::memory_order_relaxed);
            if (curr_size >= total_actors_)
                return {};
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

    // remove actor from the phonebook once it is no longer active
    void unregisterActor(const std::string& actor_name)
    {
        std::lock_guard<std::mutex> rlock(registry_lock_);
        actor_registry_.erase(actor_name);
    }

    ~ActorSystem()
    {
        for(size_t i=0;i<total_actors_;i++)
        {
            if(actor_pool_[i])
                actor_pool_[i]->stopActor();
        }
    }
};