#include <iostream>
#include <thread>
#include <chrono>
#include "actor_model_threadpool_version.h"

using Job = std::function<void()> ;

void pingpong(std::string& sender, std::string& receiver,int itr)
{
    std::cout << "Ping: " <<itr << std::endl;
    //std::cout <<sender.get() << " -> " << receiver.get()<<std::endl;
    Job getack = [](){std::cout << "Pong" << std::endl; };
    if (itr%5 == 0)
        throw std::runtime_error("Testing a exception flow");
    //receiver->send(sender,std::move(getack));
    
}

void testPingpong()
{
    std::shared_ptr<ActorSystem<Job>> ActorAdmin = std::make_shared<ActorSystem<Job>>(2);

    ActorHandle pinger = ActorAdmin->spawn(4,"Pinger");
    ActorHandle responder = ActorAdmin->spawn(4,"Responder");

    for(int i=0;i<10;i++)
    {
        ActorAdmin->send(pinger.name,responder.name,constructTask<Job>(pingpong,pinger.name,responder.name,i+1),false);
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    //pinger->sendMsg(responder,constructTask<Job>(pingpong,pinger,responder));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    return;
}

void testMultipleActors()
{
    int max_actors = 300;
    std::shared_ptr<ActorSystem<Job>> ActorAdmin = std::make_shared<ActorSystem<Job>>(max_actors);
    std::vector<ActorHandle> actor_handles;

    for(int i=0;i<max_actors;i++)
    {
        actor_handles.emplace_back(ActorAdmin->spawn(30,"actor"+to_string(i)));
    }

    for(int i=0;i<10000;i++)
    {
        int pinger = rand()%300;
        int responder = rand()%300;
        ActorAdmin->send(actor_handles[pinger].name,actor_handles[responder].name,
                        constructTask<Job>(pingpong,actor_handles[pinger].name,actor_handles[responder].name,i),false);
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
}

/*
void testActorSystem()
{
    std::shared_ptr<ActorSystem<Job>> ActorAdmin = std::make_shared<ActorSystem<Job>>(2);
    int total_msgs=10;
    std::function<void(int)> myFunc = [](int a){ std::cout << "This is my task:" << a << std::endl; };

    std::function<void(std::function<void(int)>,int)> repeated_task = [](std::function<void(int)> myFunc,int total_msgs)
                                            { 
                                                for (int i=1;i<=total_msgs;i++) 
                                                    myFunc(i);
                                            };

    auto start = std::chrono::high_resolution_clock::now();

    std::shared_ptr<Actor<Job>> asLeader = ActorAdmin->spawn(10,"Leader");
    std::shared_ptr<Actor<Job>> asActor = ActorAdmin->spawn(10,"Actor1");
    if (!asActor || !asLeader)
    {
        std::cout << "Failed to create actor" << std::endl;
        return;
    }
    bool done = false;
    //done = asLeader->send(asActor,constructTask<Job>(myFunc,20));
    for(int i=0;i<10;i++)
        done = asLeader->send(asActor,constructTask<Job>(repeated_task,myFunc,i),true);

    //ActorAdmin.send("Leader","Actor1",constructTask<Job>(repeated_task,myFunc,10));

    
    if (done)
    {
        auto end = std::chrono::high_resolution_clock::now();
        auto time_taken = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
        double throughput = ((double)total_msgs) / (time_taken / 1000.0);

        std::cout << "Elements:" << total_msgs << "\nTime taken: " << time_taken << "ms\nThroughput: " << throughput << std::endl;

    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}
*/


int main()
{
    //std::cout << std::thread::hardware_concurrency() << std::endl;
    testPingpong();
    //testMultipleActors();
    //testActorSystem();
    return 0;
}