#include <cstddef>
#include <thread>
#include <functional>
#include <semaphore>
#include <string>
#include <memory>
#include "../simple_mpmc_queue/mpmc_queue_bounded.h"

template <typename Task> class Actor;
template <typename Task> class ActorSystem;

template <typename Task>
class Actor
{
private:
    std::unique_ptr<mpmcQueueBounded<Task>> mailbox_q;
    std::atomic<bool> actor_alive_;
    std::thread dispatcher_;
    // TODO how to use counting semaphore here, while using 
    //std::binary_semaphore new_mail_arrived_{0};
    std::counting_semaphore<> new_mail_arrived_{0};

    // Open a msg from mailbox and execute the task
    void receive()
    {
        std::function<void()> newTask;
        if(mailbox_q->try_pop(newTask))
        {
#ifdef DEBUG 
            std::cout << name_ << ": "; 
#endif
            newTask();
        }
    }
    // Keep polling mailbox for new tasks
    void checkMailbox()
    {
        // TODO : if actor_alive_ is false, but the mailbox still has some tasks pending, 
        //need to add mechanims to complete those tasks
        while(actor_alive_.load(std::memory_order_relaxed))
        {
            new_mail_arrived_.acquire();
            receive();
        }
        std::cout <<name_ <<": Stopping dispatcher thread" <<std::endl;
    }

public:
    size_t id_;
    std::string name_;

    Actor(size_t mailbox_size, size_t id):actor_alive_(true),id_(id)
    {
        name_ ="";
        mailbox_q = std::make_unique<mpmcQueueBounded<Task>>(mailbox_size);
        dispatcher_ = std::thread([this](){checkMailbox();});

    }

    Actor(size_t mailbox_size, size_t id,std::string name):actor_alive_(true),id_(id),name_(name)
    {
        mailbox_q = std::make_unique<mpmcQueueBounded<Task>>(mailbox_size);
        dispatcher_ = std::thread([this](){checkMailbox();});
    }

    // Push a task to this actor's mailbox
    void addToMailbox(Task task)
    {
        if (actor_alive_.load(std::memory_order_relaxed))
        {
            while (!mailbox_q->try_push(task)){
                // TODO: add some terminate logic here to prevent spinning forever
            }   
            new_mail_arrived_.release();
        }
    }

    // Stop the mailbox checker thread
    void stopActor()
    {
        actor_alive_.store(false,std::memory_order_release);
        new_mail_arrived_.release();
        if(dispatcher_.joinable())
            dispatcher_.join();
    }

    ~Actor()
    {
        std::cout << name_ << ": Destrutor Called" << std::endl;
        if (actor_alive_)
            stopActor();
    }

    //delete copy constructors
    Actor(const Actor&) = delete;
    Actor operator=(const Actor&) = delete;

    //set move constuctors to default;
    Actor(Actor&&) = default;
    Actor& operator=(Actor &&) = default;

};

// Function that constucts the task from given parameters and adds it to the actor's mailbox
template <typename Task, typename Func, typename... Args>
void send(Actor<Task>& actor,Func&& func, Args&&... args)
{
    Task task = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
    actor.addToMailbox(task);
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
        actor_pool_=std::vector<std::shared_ptr<Actor<Task>>>(numActors,nullptr) ;
    }

    std::shared_ptr<Actor<Task>> spawn(size_t mailbox_capacity=1,std::string name = "")
    {
        size_t curr_size;
        while(1)
        {
            curr_size = active_actors_.load(std::memory_order_relaxed);
            if (curr_size >= total_actors_)
                return nullptr;
            if (active_actors_.compare_exchange_weak(curr_size,curr_size+1, std::memory_order_release,std::memory_order_acquire))
            {
                actor_pool_[curr_size] = std::make_shared<Actor<Task>>(mailbox_capacity,curr_size,name);
                return actor_pool_[curr_size];
            }
            // TODO: Do I need backoff strategy here ? 
        }
    }
};