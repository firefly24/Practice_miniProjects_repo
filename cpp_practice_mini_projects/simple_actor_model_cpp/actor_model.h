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
    std::chrono::time_point<std::chrono::steady_clock> timestamp;

    Message(){}
    explicit Message(Task&& t, std::shared_ptr<Actor<Task>> sender_m) 
        :task(std::move(t)), sender(std::weak_ptr<Actor<Task>>(sender_m))
    {
        if(sender_m)
            sender_handle = sender_m->name_;
        else
            sender_handle="ADMIN";
        timestamp = std::chrono::steady_clock::now();
    }

    // Delete copy constructor for msg to make it move only type
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;

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

    // Open a msg from mailbox and execute the task
    void receive()
    {
        Message<Task> msg;
        if(mailbox_q->try_pop(msg))
        {
            std::cout << name_ << ": "; 
            msg.task();
        }
    }

    // Once actor is stopped, flushes out mailbox
    void drainMailbox()
    {
        // Flush out all tasks remaining in the queue by executing them
        Message<Task> remainingMsg;
        while(mailbox_q->try_pop(remainingMsg))
        {
            std::cout << name_ << ": "; 
            remainingMsg.task();
        }
    }

    // Keep polling mailbox for new tasks
    void checkMailbox()
    {
        while(actor_alive_.load(std::memory_order_acquire))
        {
            new_mail_arrived_.acquire();
            receive();
        }

        // Actor is stopped, execute the remaining process in the mailbox
        drainMailbox();
        std::cout <<name_ <<": Stopping dispatcher thread" <<std::endl;
    }

public:
    size_t id_; // Keeping a actor id to identify an actor, will decide in future how to use this
    std::string name_; //Have an actor name, to get pretty logs!

    friend bool ActorSystem<Task>::send(std::string,std::string,Task && task);

    Actor(size_t mailbox_size, size_t id):actor_alive_(true),id_(id)
    {
        name_ ="";
        mailbox_q = std::make_unique<mpmcQueueBounded<Message<Task>>>(mailbox_size);
        dispatcher_ = std::thread([this](){checkMailbox();});
    }

    Actor(size_t mailbox_size, size_t id,std::string name):actor_alive_(true),id_(id),name_(name)
    {
        mailbox_q = std::make_unique<mpmcQueueBounded<Message<Task>>>(mailbox_size);
        dispatcher_ = std::thread([this](){checkMailbox();});
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
            if (!actor_alive_.load(std::memory_order_acquire))
                return false; 

            retry_count++;
            std::this_thread::yield();
            if(retry_count >16)
                return false;
        }   
        new_mail_arrived_.release();
        return true;
    }


    bool send(std::shared_ptr<Actor<Task>> receiver,Task&& task)
    {
        if(!actor_alive_.load(std::memory_order_relaxed))
            return false;
        if (receiver)
            return receiver->addToMailbox(Message<Task>{std::move(task),this->shared_from_this()});
        return false;
    }

    // Stop the mailbox checker thread
    void stopActor()
    {
        actor_alive_.store(false,std::memory_order_release);
        new_mail_arrived_.release();
        if(dispatcher_.joinable())
            dispatcher_.join();
    }

    // Destructor
    ~Actor()
    {
        std::cout << name_ << ": Destrutor Called" << std::endl;
        if (actor_alive_.load())
            stopActor();
    }

    //delete copy constructors
    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    //set move constuctors to delete;
    Actor(Actor&&) = delete;
    Actor& operator=(Actor &&) = delete;

};

template <typename Task, typename Func, typename... Args>
Task constructTask(Func&& func, Args&&... args)
{
    Task task = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
    return task;
}

template <typename Task>
class ActorSystem
{
private:
    std::atomic<size_t> active_actors_;
    size_t total_actors_;
    std::mutex registry_lock_;
    std::unordered_map<std::string,std::weak_ptr<Actor<Task>>> actor_registry_;

public:

    std::vector<std::shared_ptr<Actor<Task>>> actor_pool_;

    ActorSystem(size_t numActors):   active_actors_(0), total_actors_(numActors) {
        actor_pool_=std::vector<std::shared_ptr<Actor<Task>>>(numActors) ;
    }

    bool registerActor(std::shared_ptr<Actor<Task>> actor)
    {
        if(actor_registry_[actor->name_].lock())
        {
            std::cout << "Name is taken" << std::endl;
            return false;
        }
        std::lock_guard<std::mutex> rlock(registry_lock_);
        actor_registry_[actor->name_] = std::weak_ptr<Actor<Task>>(actor);
        return true;
    }

    std::shared_ptr<Actor<Task>> getActor(std::string actor_name)
    {
        std::lock_guard<std::mutex> rlock(registry_lock_);
        if(actor_registry_.find(actor_name) == actor_registry_.end())
            return {};
        return actor_registry_[actor_name].lock();
    }

    std::shared_ptr<Actor<Task>> spawn(size_t mailbox_capacity=1,std::string name="")
    {
        size_t curr_size;
        size_t retry_count=0;
        if (actor_registry_.find(name) != actor_registry_.end())
        {
            std::cout << "Actor name already exists!" << std::endl;
            return {};
        }
        while(1)
        {
            curr_size = active_actors_.load(std::memory_order_relaxed);
            if (curr_size >= total_actors_)
                return {};
            if (active_actors_.compare_exchange_weak(curr_size,curr_size+1, std::memory_order_release,std::memory_order_acquire))
            {
                actor_pool_[curr_size] = std::make_shared<Actor<Task>>(mailbox_capacity,curr_size,name);
                registerActor(actor_pool_[curr_size]);
                return actor_pool_[curr_size];
            }
            retry_count++;
            if (retry_count>=16)
                return {};
        }
    }

    // Helper function to send msg based on actor name instead of pointers, from sender -> receiver actor
    bool send(std::string sender_handle,std::string receiver_handle, Task&& task)
    {   
        auto receiver = getActor(receiver_handle);
        auto sender = getActor(sender_handle);
        if (!receiver || !sender)
            return false;        

        return sender->send(receiver,std::move(task));
    }

    // Helper function overload, directly using actor pointers
    bool send(std::shared_ptr<Actor<Task>> sender,std::shared_ptr<Actor<Task>> receiver,Task&& task)
    {
        if(!receiver || !sender)
            return false;
        return sender->send(receiver,std::move(task));
    }

    bool send(std::string receiver_handle, Task&& task)
    {   
        auto receiver = getActor(receiver_handle);
        if (!receiver)
            return false;

        if ( !receiver->addToMailbox(Message<Task>{std::move(task),{}}) )
        {
            std::cout << receiver->name_ <<":Actor is stopped! Mailbox is closed for new mails!" << std::endl;
            return false;
        }
        return true;
    }

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
            {
                unregisterActor(actor_pool_[i]->name_);
                actor_pool_[i]->stopActor();
            }
        }
    }
};