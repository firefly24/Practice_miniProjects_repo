#!/usr/bin/env python3

import sys, csv, math
from collections import defaultdict, deque
from datetime import datetime
import matplotlib.pyplot as plt
import pandas as pd

#Parse the logs into event types
class Event:
    timestamp: int
    actor_id: int
    gen_id: int
    thread_id: str
    eventType: str

#load log csv into dataframe
def parse_csv(log_path):
    evtlog = pd.read_csv(log_path, dtype= {"thread_id":"string","eventType":"string"})
    evtlog=evtlog.sort_values(by="timestamp").reset_index(drop = True)
    evtlog["ts"] = pd.to_datetime(evtlog["timestamp"], unit="ms")
    evtlog.info()
    return evtlog

#separate data per actor
def getActorData(evtlog,actorId):
    actor_data = evtlog[evtlog['actor_id'] == actorId]
    return actor_data

#draw a basic plot of stats per actor
def plotActorData(evtlog,actor_id):
    # I will configure the profiler plots here
    print(f"Plotting for actor_id {actor_id}")
    actor_log = getMailboxPlot(evtlog,actor_id)

    #Create new columns for mailbox data
    actor_log["Enqueue_sum"] = actor_log["Enqueue"].cumsum()
    actor_log["Dequeue_sum"] = actor_log["Dequeue"].cumsum()
    actor_log["mailbox_content"] = actor_log["Enqueue_sum"] - actor_log["Dequeue_sum"]
    assert (actor_log["mailbox_content"] >= 0).all(), "there is a bug! Dequeues > Enqueues should not happen!"

    #plot enqueue, dequeue, mailbox contents, and restart events for the actor
    #use timestamp as int64 for plotting, as it is easier to visualize per ms changes
    fig,ax = plt.subplots(figsize = (10,6))
    ax.plot(actor_log["timestamp"],actor_log["Enqueue"], label = "Enqueue", marker = 'o',ls = "")
    ax.plot(actor_log["timestamp"],actor_log["Dequeue"], label = "Dequeue", marker = 'x',ls = "")
    ax.plot(actor_log["timestamp"],actor_log["mailbox_content"], label = "mailbox", marker = '+')

    #mark restart events on the plot
    if "Restart" in actor_log.columns:
        for idx,row in actor_log.iterrows():
            if row["Restart"] == 1.0:
                ax.axvline(x= row["timestamp"],color = "magenta",alpha = 0.3)

    plt.xlabel("Timestamp in ms")
    plt.ylabel("Events per ms/Mailbox depth")

    plt.title(f"Mailbox stats for Actor: {actor_id} ")

    print(actor_log)
    plt.legend()
    plt.show()

def getMaxActors(evtlog):
    return evtlog["actor_id"].max() +1

def summaryByEvent(evtlog):
    return evtlog.groupby(by=["actor_id","eventType"]).size().rename("count").reset_index().pivot(index = "actor_id", columns = "eventType", values = "count")

def getMailboxPlot(evtlog,actorId):
    actor_data = evtlog[(evtlog["actor_id"] == actorId)]
    actor_data_tab= actor_data.groupby(by = ["timestamp","eventType"]).size().rename("count").reset_index().pivot(index = "timestamp", columns = "eventType", values = "count").fillna(0).sort_index().reset_index()
    return actor_data_tab

#Main function
print("This program")
evtlog = parse_csv("./log/actor_trace_1762673233742.csv")

# get idea of what the data looks like 
print(evtlog.head())
#print (getActorData(evtlog,0).head())
#print(getMaxActors(evtlog))
print(summaryByEvent(evtlog))
print()
print()
#print(getMailboxPlot(evtlog,0))
plotActorData(evtlog,5)

