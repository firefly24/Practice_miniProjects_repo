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
