#!/usr/bin/env python3

import sys, csv, math
import argparse
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

def getMailboxPlot(evtlog,actorId):
    actor_data = evtlog[(evtlog["actor_id"] == actorId)]
    actor_data_tab= actor_data.groupby(by = ["timestamp","eventType"]).size().rename("count").reset_index().pivot(index = "timestamp", columns = "eventType", values = "count").fillna(0).sort_index().reset_index()
    
    if "Enqueue" not in actor_data_tab.columns:
        actor_data_tab["Enqueue"] = 0
    if "Dequeue" not in actor_data_tab.columns:
        actor_data_tab["Dequeue"] = 0
    return actor_data_tab

#draw a basic plot of stats per actor
def plotActorData(evtlog,actor_ids):

    num_actors = len(actor_ids)

    fig,ax = plt.subplots(num_actors+1,1,figsize = (15,(5*num_actors)),sharex=True)
    plot_row = 0
    stoptime = evtlog[evtlog["eventType"] == "StopSystem"]["timestamp"].reset_index(drop = True)
    print(stoptime[0])

    for actor_id in actor_ids:
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
        
        ax[plot_row].step(actor_log["timestamp"],actor_log["Enqueue"], label = "Enqueue", marker = 'o',color='g')
        ax[plot_row].step(actor_log["timestamp"],actor_log["Dequeue"], label = "Dequeue", marker = 'x',ls = "",color = 'r')
        ax[plot_row].step(actor_log["timestamp"],actor_log["mailbox_content"], label = "mailbox")
        ax[plot_row].axvline(x = stoptime[0], color = 'm', linewidth = 2 )

        #mark restart events on the plot
        if "Restart" in actor_log.columns:
            restart_lines = actor_log[actor_log["Restart"] >0]["timestamp"]
            for xr in restart_lines:
                    ax[plot_row].axvline(x= xr ,color = "magenta",alpha = 0.1, linewidth = 1)



        ax[plot_row].set_title(f"Mailbox stats for Actor: {actor_id} ")
        ax[plot_row].ticklabel_format(axis = 'x', style = 'plain')
        plot_row+=1

    # Plot threadpool queue stats
    pool_stats = evtlog[(evtlog["eventType"] == "PoolEnqueue") | (evtlog["eventType"] == "PoolDequeue")].groupby(by=["eventType","timestamp"]).size().rename("count").reset_index()
    pool_stats = pool_stats.pivot( index = "timestamp", columns = "eventType", values = "count" ).reset_index()
    pool_stats["PoolEnqueue_sum"] = pool_stats["PoolEnqueue"].cumsum()
    pool_stats["PoolDequeue_sum"] = pool_stats["PoolDequeue"].cumsum()
    pool_stats["Pool_depth"] = pool_stats["PoolEnqueue_sum"] - pool_stats["PoolDequeue_sum"]


    ax[plot_row].step(pool_stats["timestamp"],pool_stats["PoolEnqueue"], label = "Enqueue", marker = 'o',color='g')
    ax[plot_row].step(pool_stats["timestamp"],pool_stats["PoolDequeue"], label = "Dequeue", marker = 'x',ls = "",color = 'r')
    #ax[plot_row].step(pool_stats["timestamp"],pool_stats["Pool_depth"], label = "mailbox")
    ax[plot_row].axvline(x = stoptime[0], color = 'm', linewidth = 2 )
    

    #Experimental
    pool_stats_fail = evtlog[(evtlog["eventType"] == "PoolEnqueue") | (evtlog["eventType"] == "PoolDequeue")].drop(["actor_id","gen_id","thread_id","ts"],axis=1).reset_index(drop="True")
    pool_stats_fail["pool_depth"] = 0
    pdepth = 0

    for idx,row in pool_stats_fail.iterrows():
        if row["eventType"] == "PoolEnqueue":
            pdepth+=1
        else:
            pdepth-=1
        pool_stats_fail.at[idx,"pool_depth"] = pdepth
    
    ax[plot_row].step(pool_stats_fail["timestamp"],pool_stats_fail["pool_depth"], label = "mailbox")

    print(pool_stats_fail)

    plt.xlabel("Timestamp in ms")
    plt.ylabel("Events per ms/Mailbox depth")
    plt.legend()
    plt.savefig("myProfileStats.png",dpi = 300)
    plt.show()


def getMaxActors(evtlog):
    return evtlog["actor_id"].max()+1

def summaryByEvent(evtlog):
    return evtlog.groupby(by=["actor_id","eventType"]).size().rename("count").reset_index().pivot(index = "actor_id", columns = "eventType", values = "count")

#Main function
print("This program")

parser = argparse.ArgumentParser("Parsing cmd line arguments")
parser.add_argument("logpath", type=str,help = "Name of log file in ./log directory")
args = parser.parse_args()

evtlog = parse_csv(f"{args.logpath}")

# get idea of what the data looks like 
print(evtlog.head())
#print (getActorData(evtlog,0).head())
#print(getMaxActors(evtlog))
print(summaryByEvent(evtlog))
print()
print()
max_actors = getMaxActors(evtlog)
#print(getMailboxPlot(evtlog,0))
plotActorData(evtlog,[i for i in range(0,max_actors)])

