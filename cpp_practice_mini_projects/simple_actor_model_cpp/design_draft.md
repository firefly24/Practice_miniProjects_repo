A simple Actor System Design

1. Core Goals:
- I am trying to design a simple actor framework model in c++ which might be useful as a lightweight message passing mechanism in any embedded/low latency systems projects I might take up in future.
- I want to learn to build concurrency systems through this project, and learn to use lock-free concurrency concepts, mutexes, and optimizing thread usage. 
- Eventually I want to improve my design incrementally and learn various optimization techniques and how to handle design trade-offs etc.

2. Main core components of my system: 

Task - a unit of work we want to get performed

Actor - it is the main entity of the system that is responsible for communication, it is responsible for processing any msgs/tasks that are pushed into its mailbox. In User programs, an actor can be used via its Actor handle.

ActorSystem – It represents the actor system as a whole, and it owns all actors it spawned, it manages the lifecyle and choreograhs all the actors it owns. It also holds ownership of a threadpool, which has worker threads to execute actors' tasks.

ActorHandle – This is a lightweight referencing mechanism to address any actor by the user program - which uses an actor id, or actor name, or both.

ThreadPool – a set of worker threads that pick up and execute the pending/live tasks in actors' mailboxes.

Message – wraps a task and metadata into a single Msg object, so that it can be a uniform entity to pass seamlessly around mailboxes.

# Add image for control flow strategy 

3. Ownership Model 
ActorSystem -> owns a pool unique Actors, owns a Threadpool, owns a Cleanup-recovery thread
Actor -> owns a Mailbox, it is a unique_ptr
Mailbox -> holds a lists of Tasks for the actor
Task -> a small unit of work
Threadpool ->owns few Worker threads
Cleanup-recovery thread -> actively cleans up and recovers failed actors
Worker threads -> execute tasks from actor Mailbox
Message -> encapsulates Task and its metadata
ActorHandle -> a lightweight non-owning reference to an actor

4. Actor Lookup strategy
- User program can access an Actor through an ActorHandle {Id,name}
- For fast lookup of an Actor, use actor Id to reference the required actor from Actor Pool
- In cases were actor id can't be used, for better readability actor name can be used. It involves looking up the actor id using actor name in the actor registry map : actor_name -> actor_id
- If an actor fails, a new actor unique_ptr object is created for the same id and name, replacing the failed actor object.
So for any external user, the handle remains the same, and it does not need to handle any failure/recovery explicitly.

5. Message Flow
- An Sender_Actor::send() triggers a send to Receiver_Actor. Sender_Actor::send() notifies the ActorSystem to route the msg to correct receiver. ActorSystem::send() will then lookup the Receiver_Actor by name and add the Msg to Receiver_Actor::AddtoMailbox(msg).
- If the mailbox of Receiver_Actor was previously empty, trigger a drainMailbox() from the Threadpool to schedule execution of the queued msgs.

6. ThreadPool usage
- Schedules the tasks queued in actor Mailbox to be executed by worker threads
- An Actor's queued tasks will not run concurrently at the same time on different threads.
- This threadpool also has a fixed size internal queue where the drains will wait to be picked up. I am not expecting the need for unbounded queue here, as this queue does not hold the actual number of tasks, but just a notifiction to trigger drainMailbox

7. Recovery and Fault Handling
- If a Task throws -> the owning Actor fails -> Actor stops new msgs/tasks and notifies ActorSystem of Failure -> ActorSystem will log the failure -> ActorSystem unregister the Actor and destroy the failed Actor object cleanly -> ActorSystem spawns a new duplicate Actor with the same Id and name and replaces the old Actor.

8. Open Questions
- How to handle reply mechanisms, when an actor expects a receiver reply or an acknowledgement
- Do we drop pending mailbox msgs of failed actor, or carry them forward to be executed by restarted actor
- How do we handle the msgs lost between Actor Fail to Restart, do we keep waiting for Actor to respawn or drop them.
- How to handle tasks that have a return value, how big is the impact of using packaged_tasks/futures/asyncs for this
- What all recovery strategies we can add later on apart from restart.
- Implement Supervision tree style actor framework later if possible.

9. Future Enhancements
- Add reply mechanism
- add different scheduling policies per Actor
- Add more Actor recovery strategies
- Add logging and performance metrics data collection
- Add live queriying of system health stats



