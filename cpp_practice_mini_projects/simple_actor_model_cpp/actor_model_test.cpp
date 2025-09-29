#include <iostream>
#include <thread>
#include <chrono>
#include "actor_model.h"

using Job = std::function<void()> ;


int main()
{
    Actor<Job> dummy{10};

    std::function<void(int)> myFunc = [](int a){ std::cout << "This is my task" << a << std::endl; };

    for (int i=1;i<=10;i++)
    {
        send(dummy,myFunc,i);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    dummy.stopActor();

    //for (int i=1;i<=10;i++)
    {
        send(dummy,myFunc,11);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    return 0;
}