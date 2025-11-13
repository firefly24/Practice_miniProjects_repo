Implemented a simple actor model framework and actor system to send messages to actors

added a send funtion to wrap any function with parameters to a function<void()> type and push to mailbox

Now I want to send the sender actor info with the function task as well.
For scenarios like, the task in the inbox is supposed to print the name of the executing actor


03/10/25
Next scenario to solve: 
et's see an example: actor1 ,actor2,actor3 are actors and we send actor1 a message ping, now actor 1 should reply "foo" if ping was sent by actor2, or "bar" if ping was sent by actor3


05/10/2025
I am sending messages of struct {senderActor,receiverActor,task} instead of just task, so that each actor knows where the msg comes from.
Now instead of senderActor to be just a reference to actor, I can send a handle to the senderActor as reply_to:
if the sender expects a reply, attach sender reference to msg.reply_to , if sender doesn't expect a response, this can be set to null.
Can I remove "receiverActor" field from msg struct ? 

11/10/2025
Write my own RAII style guard for atomic counter -(Found no use for it so erased it)


12/10/2025
Moved the actor model to threadpool type execution, as previously each actor had its own dispatcher thread, which is not scalable as thousands of actors might be created.
I need to refine the actor recovery mechanism now, as when an actor fails, in most cases the old actor shared ptr is still used instead of the respawned actor

Focus on:
Move semantics, perfect forwarding — understand how they affect performance and design.
Templates & type traits (std::enable_if, concepts, SFINAE basics).
RAII patterns — smart pointer strategies, scoped cleanup, unique_ptr + custom deleters.
Standard concurrency utilities — std::jthread, std::stop_token, std::latch, std::barrier.

14/10/2025:
1. Actor system hold the ownership of all actors inside the system, and any external party should use only actor handles to access the actors. 
2. For send(), notify new msg etc, the actor only notifies the actorsystem, and the latter is responsible for actually handling the tasks. 
    ->Scheduling/dispatch — which thread runs what → belongs to ActorSystem
    ->Mail delivery — pushing messages into mailboxes → belongs to the receiver actor
3. In case of actor and failure and recovery, the new spawned actor's pointer will change, but handle is consistent

Actors → do work, know only handles.
ActorSystem → owns actors, does all routing.
Handles → the only public-facing identifiers.
send() → user always calls through ActorSystem.

15/10/2025

Feeling ambitious, changing actors ref from shared ptr to unqiue ptrs

task fails -> actor_alive set to false -> addToMailBox stops pushing new msgs -> drainMailBox stops executing new msgs -> push actor id in cleanup queue -> copy actor parameters and unregister(failed actor) -> in unregister() call , remove entry from actor_registry_ -> Call Actor.reset() -> destructor is called -> destructor calls stopActor() -> stopActor waits for is_draining_ to be false


15/11/2025
I was able to build a small visualizer about the mailbox activity of each actor into matplotlib. 
I observed a variable slump/stall window in the activity of the overall actor system, where all actors show no activity at the same time during the 12-50 ms stall window.
I observed it on only some of the runs, so repro rate is not 100%

I assumed the stall was due to the threadpool task queue being full, but when I also tracked the threadpool task queue size, I don't see it being full (Last plot on the profiler graph).
![issue image](myProfileStats_buggy2.png)

Now I suspect the stall might be due to either the locks/condition_variables used in threadpool queue, or the registy_lock_ I am using in send.
(Need to move to lock-free queue in threapool implementation, and change the send API in actor model to remove registry_lock dependency and see if issue still persists.)

I don't plan to tackle this soon, and would rather sleep on it for now as these are big changes. I want to spend some time at this moment to just try to get my profiler graphs more prettier to get a mood boost.

