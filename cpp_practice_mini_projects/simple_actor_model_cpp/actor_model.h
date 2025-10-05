#include <cstddef>
#include <thread>
#include <functional>
#include <semaphore>
#include <string>
#include <memory>
#include <iostream>
#include "../simple_mpmc_queue/mpmc_queue_bounded.h"

template <typename Task> class Actor;
template <typename Task> class ActorSystem;

template <typename Task>
struct Message{
    Task task;
    //std::weak_ptr<void> sender;
    //std::weak_ptr<void> receiver;
    std::weak_ptr<Actor<Task>> sender;
    std::weak_ptr<Actor<Task>> receiver;
    Message(){}
    //Message(Task&& t):task(std::move(t)), sender{}, receiver{} {}

    explicit Message(Task&& t, std::weak_ptr<Actor<Task>> sender_m, std::weak_ptr<Actor<Task>> receiver_m) 
            :task(std::move(t)), sender(sender_m), receiver(receiver_m) {}

    Message(const Message&) = delete;
    Message operator=(const Message&) = delete;

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
            //std::cout << name_ << ": "; 
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
            //std::cout << name_ << ": "; 
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
            //add some terminate logic here to prevent spinning forever
            retry_count++;
            if(retry_count >16)
                return false;
        }   
        new_mail_arrived_.release();
        return true;
    }


    bool sendMsg(std::shared_ptr<Actor<Task>> receiver,Task&& task)
    {
        if(!actor_alive_.load(std::memory_order_relaxed))
            return false;
        //Message<Task> newMsg{this,receiver,task};
        if (receiver)
            return receiver->addToMailbox(Message<Task>{std::move(task),std::weak_ptr<Actor<Task>>(this->shared_from_this()),std::weak_ptr<Actor<Task>>(receiver)});
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

// Function that constucts the task from given parameters and adds it to the actor's mailbox
template <typename Task, typename Func, typename... Args>
void send(std::shared_ptr<Actor<Task>> sender,std::shared_ptr<Actor<Task>> receiver,Func&& func, Args&&... args)
{
    Task task = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
    //Message<Task> new_msg{task,std::weak_ptr<Actor<Task>>{},std::weak_ptr<Actor<Task>>(receiver)};
    
    if (!receiver->addToMailbox(Message<Task>(std::move(task),std::weak_ptr<Actor<Task>>(sender),std::weak_ptr<Actor<Task>>(receiver))))
        std::cout << "Actor is stopped! Mailbox is closed for new mails!" << std::endl;
    return;
}

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

public:

    std::vector<std::shared_ptr<Actor<Task>>> actor_pool_;
    ActorSystem(size_t numActors):   active_actors_(0), total_actors_(numActors) {
        actor_pool_=std::vector<std::shared_ptr<Actor<Task>>>(numActors) ;
    }

    std::shared_ptr<Actor<Task>> spawn(size_t mailbox_capacity=1,std::string name = "")
    {
        size_t curr_size;
        size_t retry_count=0;
        while(1)
        {
            curr_size = active_actors_.load(std::memory_order_relaxed);
            if (curr_size >= total_actors_)
                return {};
            if (active_actors_.compare_exchange_weak(curr_size,curr_size+1, std::memory_order_release,std::memory_order_acquire))
            {
                actor_pool_[curr_size] = std::make_shared<Actor<Task>>(mailbox_capacity,curr_size,name);
                return actor_pool_[curr_size];
            }
            retry_count++;
            if (retry_count>=16)
                return {};
        }
    }
};