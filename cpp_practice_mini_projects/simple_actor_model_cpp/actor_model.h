#include <cstddef>
#include <thread>
#include <functional>
#include "../simple_mpmc_queue/mpmc_queue_bounded.h"

template <typename Task> class Actor;
template <typename Task> class ActorModel;

template <typename T>
void send_msg(Actor<T>&,T);

template <typename Task>
class Actor
{

private:
    size_t id_;
    mpmcQueueBounded<Task> mailbox_q;
    std::atomic<bool> actor_alive_;
    std::thread dispatcher_;

    void receive()
    {
        //std::cout << "receive" << std::endl;
        std::function<void()> newTask;
        if(mailbox_q.try_pop(newTask)) 
            newTask();
        
    }

    void checkMailbox()
    {
        while(actor_alive_.load(std::memory_order_relaxed))
        {
            
            if (!mailbox_q.empty())
                receive();
        }
        //std::cout << "Stopping dispatcher thread" <<std::endl;
    }

public:
    Actor(size_t mailbox_size): mailbox_q(mailbox_size), actor_alive_(true)
    {
        dispatcher_ = std::thread([this](){checkMailbox();});
    }

    friend void send_msg<Task>(Actor<Task>&,Task);

    void stopActor()
    {
        actor_alive_.store(false);
        if(dispatcher_.joinable())
            dispatcher_.join();
    }

    ~Actor()
    {
        //std::cout << "Desctrutor Called" << std::endl;
        if (actor_alive_)
            stopActor();
    }
};

template <typename Task>
void send_msg(Actor<Task>& actor,Task task)
{
    while (!actor.mailbox_q.try_push(task)){}   
}

template <typename Task, typename Func, typename... Args>
void send(Actor<Task>& actor,Func&& func, Args&&... args)
{
    Task task = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
    send_msg(actor,task);
}