# Simple Actor System Design (Draft)

## 1. Core Goals

- **Purpose:** Design a lightweight C++ Actor Framework suitable for embedded or low-latency systems.
- **Learning Focus:** Explore concurrency patterns, lock-free design, mutex usage, and thread optimization.
- **Long-Term Objective:** Incrementally improve the system and gain experience with optimization, recovery handling, and design trade-offs.

---

## 2. Core Components

| Component | Description |
|------------|--------------|
| **Task** | A unit of work to be performed by an actor. |
| **Message** | Wraps a Task and metadata into a single transferable entity. |
| **Actor** | Main active entity that processes messages from its mailbox. |
| **ActorSystem** | The orchestrator that owns all actors, manages lifecycles, threadpools, and recovery. |
| **ActorHandle** | A lightweight, non-owning reference to an actor, identified by `{id, name}`. |
| **Mailbox** | Internal queue that buffers tasks/messages awaiting execution. |
| **ThreadPool** | Worker threads that drain actor mailboxes and execute queued tasks. |
| **Cleanup/Recovery Thread** | Monitors actor failures and respawns replacements. |

> 💡 *Future addition: Insert diagram of component relationships.*

---

## 3. Ownership Model

**Notes:**
- `ActorSystem` has exclusive ownership of all actors (`unique_ptr`).
- Each `Actor` owns its `Mailbox`, which contains `Task` objects.
- `ActorHandle` is a *non-owning reference* (stores `{id, name}` only).
- `ThreadPool` workers execute mailbox tasks, but **tasks from a single actor never run concurrently.**

> 💡 Consider adding a short “Design Invariants” section:
> - One `drainMailbox()` per actor at a time  
> - Actor lifetimes managed solely by `ActorSystem`  
> - `ActorHandle` validity checked before routing

---

## 4. Actor Lookup Strategy

- **Primary lookup:** via `actor_id` → O(1) access in `actor_pool_`.
- **Secondary lookup:** via `actor_name` → resolved to `actor_id` through an `actor_registry` map (`std::unordered_map<std::string, size_t>`).
- **Failure replacement:**  
  When an actor fails:
  - The same `{id, name}` entry is reused for the new actor.
  - `ActorHandle` remains valid across restarts — user code does not manage recovery.

> 💡 Optional enhancement: add lookup caching or lock-free registry map if scalability becomes an issue.

---

## 5. Message Flow

> 💡 *Placeholder: Add a sequence diagram here.*

Example flow:

**Key Rules:**
- If ReceiverActor’s mailbox transitions from empty → non-empty, the system triggers a drain request to the threadpool.
- Threadpool workers process messages sequentially per actor.
- Tasks from the same actor **never execute concurrently.**

---

## 6. ThreadPool Model

- Fixed-size pool of worker threads.
- Uses an internal **drain queue**, not a task queue — each entry signals that an actor’s mailbox needs draining.
- The threadpool repeatedly:
  1. Waits for drain requests.
  2. Dequeues one.
  3. Executes that actor’s `drainMailbox()` until empty.
- Prevents multiple workers from draining the same actor simultaneously.

> 💡 Consider naming this queue `drain_queue_` to distinguish it from standard task queues.

---

## 7. Recovery and Fault Handling

1. **If a task throws:**
   - The owning actor is marked as *failed*.
   - The actor stops accepting new messages.
   - ActorSystem logs the failure and unregisters the actor.
   - The cleanup thread destroys the failed actor (`unique_ptr::reset()`).
   - A new actor instance is spawned with the same `{id, name}` and replaces it.

2. **Design note:**  
   Pending messages can either:
   - Be dropped, or  
   - Be re-queued to the new actor’s mailbox.

> 💡 Decision pending — clarify whether mailboxes are reset or transferred after recovery.

---

## 8. Open Questions

- How to design reply or acknowledgement mechanisms between actors?
- Should pending mailbox messages of a failed actor be preserved or dropped?
- What happens to messages sent during the actor’s restart window?
- How to support return values from tasks (e.g., via `std::future` / `packaged_task`)?
- What recovery strategies can we support beyond restart (e.g., exponential backoff, fail-fast, quarantine)?
- Could we later introduce **supervision trees** like Erlang’s model?

---

## 9. Future Enhancements

- [ ] Implement reply/ack mechanisms between actors.  
- [ ] Add configurable actor scheduling policies.  
- [ ] Extend recovery strategies (restart, escalate, stop).  
- [ ] Integrate logging and performance metrics.  
- [ ] Add live system health monitoring APIs.  
- [ ] Implement supervision trees for hierarchical fault tolerance.  
- [ ] Add metrics for mailbox utilization and message latency.

---

## 10. References (Optional)

- [Erlang/OTP Design Principles](https://www.erlang.org/doc/design_principles/des_princ)
- [CAF: C++ Actor Framework](https://actor-framework.org/)
- [Akka Actor Model Concepts](https://doc.akka.io/docs/akka/current/typed/actors.html)

---

> ✳️ *Draft version by Darshana Hotkar – ongoing experimental project for learning concurrency and actor-based system design in C++.*

