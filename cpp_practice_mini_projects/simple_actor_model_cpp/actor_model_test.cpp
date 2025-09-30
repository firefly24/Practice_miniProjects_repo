#include <iostream>
#include <thread>
#include <chrono>
#include "actor_model.h"

using Job = std::function<void()> ;

void pingpong(Actor<Job>& sender, Actor<Job>& receiver)
{
    std::cout << "Ping" << std::endl;

    auto getack = [](){std::cout << "Pong" << std::endl; };

    send(sender,getack);
}


void testPingpong()
{
    Actor<Job> dummy{5,1,"dummy1"};
    Actor<Job> dummy2{5,2,"dummy2"};

    send(dummy2,pingpong,std::ref(dummy),std::ref(dummy2));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return;
}

void testActorSystem()
{
    ActorSystem<Job> ActorAdmin(1);
    int total_msgs=20;
    std::function<void(int)> myFunc = [](int a){ std::cout << "This is my task:" << a << std::endl; };

    auto start = std::chrono::high_resolution_clock::now();

    std::shared_ptr<Actor<Job>> asActor = ActorAdmin.spawn(10,"Actor1");

    for (int i=1;i<=total_msgs;i++)
    {
        send(*asActor,myFunc,i);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto end = std::chrono::high_resolution_clock::now();

    auto time_taken = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
    double throughput = ((double)total_msgs) / (time_taken / 1000.0);

    std::cout << "Elements:" << total_msgs << "\nTime taken: " << time_taken << "ms\nThroughput: " << throughput << std::endl;
}

int main()
{
    testPingpong();
    testActorSystem();
    return 0;
}