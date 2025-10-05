#include <iostream>
#include <thread>
#include <chrono>
#include "actor_model.h"

using Job = std::function<void()> ;

void pingpong(std::shared_ptr<Actor<Job>> sender, std::shared_ptr<Actor<Job>> receiver)
{
    std::cout << "Ping" << std::endl;

    Job getack = [](){std::cout << "Pong" << std::endl; };
    receiver->send(sender,std::move(getack));
}

void testPingpong()
{
    ActorSystem<Job> ActorAdmin(2);

    std::shared_ptr<Actor<Job>> pinger = ActorAdmin.spawn(3,"Pinger");
    std::shared_ptr<Actor<Job>> responder = ActorAdmin.spawn(3,"Responder");

    //for(int i=0;i<10;i++)
        ActorAdmin.send("Pinger","Responder",constructTask<Job>(pingpong,pinger,responder));

    //pinger->sendMsg(responder,constructTask<Job>(pingpong,pinger,responder));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return;
}

void testActorSystem()
{
    ActorSystem<Job> ActorAdmin(2);
    int total_msgs=20;
    std::function<void(int)> myFunc = [](int a){ std::cout << "This is my task:" << a << std::endl; };

    std::function<void(std::function<void(int)>,int)> repeated_task = [](std::function<void(int)> myFunc,int total_msgs)
                                            { 
                                                for (int i=1;i<=total_msgs;i++) 
                                                    myFunc(i);
                                            };

    auto start = std::chrono::high_resolution_clock::now();

    std::shared_ptr<Actor<Job>> asLeader = ActorAdmin.spawn(10,"Leader");
    std::shared_ptr<Actor<Job>> asActor = ActorAdmin.spawn(10,"Actor1");
    if (!asActor || !asLeader)
    {
        std::cout << "Failed to create actor" << std::endl;
        return;
    }
    bool done = false;
    //done = asLeader->sendMsg(asActor,constructTask<Job>(myFunc,20));
    //for(int i=0;i<20;i++)
    //    done = asLeader->send(asActor,constructTask<Job>(repeated_task,myFunc,10));

    ActorAdmin.send("Leader","Actor1",constructTask<Job>(repeated_task,myFunc,10));

    if (done)
    {
        auto end = std::chrono::high_resolution_clock::now();
        auto time_taken = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
        double throughput = ((double)total_msgs) / (time_taken / 1000.0);

        std::cout << "Elements:" << total_msgs << "\nTime taken: " << time_taken << "ms\nThroughput: " << throughput << std::endl;

    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

int main()
{
    testPingpong();
    testActorSystem();
    return 0;
}