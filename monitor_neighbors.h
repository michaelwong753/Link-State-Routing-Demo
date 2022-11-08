#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/time.h>
#include <map>
#include <vector>
#include <fstream>
#include "json.hpp"

using json = nlohmann::json;

struct LSANode{
		int seq;
		std::map<int,long int> neighbors;
};


void to_json(json& j, const LSANode& l){
	j = {
		{"seq", l.seq},
		{"neighbors", l.neighbors}
	};
}

void from_json(const json& j, LSANode& l){
	j.at("seq").get_to(l.seq);
	j.at("neighbors").get_to(l.neighbors);
}


int globalMyID = 0;
//last time you heard from each node. TODO: you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
struct sockaddr_in globalNodeAddrs[256];

std::map<int, LSANode*> node_cost_map;


//Yes, this is terrible. It's also terrible that, in Linux, a socket
//can't receive broadcast packets unless it's bound to INADDR_ANY,
//which we can't do in this assignment.
void hackyBroadcast(const char* buf, int length)
{
	int i;
	for(i=0;i<256;i++)
		if(i != globalMyID) //(although with a real broadcast you would also get the packet yourself)
			sendto(globalSocketUDP, buf, length, 0,
				  (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
}

void hackyBroadcastNeighbor(const char* buf, int length, short int heardFrom)
{
	int i;
	std::map<int, long int>::iterator it_hackyn;
    for (it_hackyn = node_cost_map[globalMyID]->neighbors.begin();it_hackyn != node_cost_map[globalMyID]->neighbors.end();it_hackyn++){
		if (it_hackyn->first != heardFrom){
			sendto(globalSocketUDP, buf, length, 0, (struct sockaddr*)&globalNodeAddrs[it_hackyn->first], sizeof(globalNodeAddrs[it_hackyn->first]));
		}
	}
}

void hackyBroadcastID(const char* buf, int length, int ID)
{
	sendto(globalSocketUDP, buf, length, 0, (struct sockaddr*)&globalNodeAddrs[ID], sizeof(globalNodeAddrs[ID]));
}

int minDistance(std::map<int,int> dist, std::map<int,bool> visited)
{
    // Initialize min value
    int min = INT_MAX;
	int min_index;
	std::map<int, LSANode*>::iterator it_min;
 	for (it_min = node_cost_map.begin();it_min != node_cost_map.end();it_min++){
        if (visited[it_min->first] == false && dist[it_min->first] <= min){
            min = dist[it_min->first];
			min_index = it_min->first;
		}
	}
 
    return min_index;
}
 
void printSolution(std::map<int,int> dist, std::map<int,int> parent)
{
    printf("Vertex   Distance from Source\n");
	std::map<int, LSANode*>::iterator it_print;
    for (it_print = node_cost_map.begin();it_print != node_cost_map.end();it_print++){
        printf("%d \t\t %d\n", it_print->first, dist[it_print->first]);
	}

	printf("Vertex   Parents\n");
	std::map<int, LSANode*>::iterator it_parents;
    for (it_parents = node_cost_map.begin();it_parents != node_cost_map.end();it_parents++){
        printf("%d \t\t %d\n", it_parents->first, parent[it_parents->first]);
	}
}
 

void djikstra(int src, std::map<int,int> &parent, std::map<int,int> &dist){	
    std::map<int,bool> visited;
	dist = {};
	parent = {};
	std::map<int, LSANode*>::iterator it_init;
    for (it_init = node_cost_map.begin();it_init != node_cost_map.end();it_init++){
        dist[it_init->first] = INT_MAX;
		visited[it_init->first] = false;
	}
 
    // Distance of source vertex from itself is always 0
    dist[src] = 0;
    // Find shortest path for all vertices
	std::map<int, LSANode*>::iterator it_outer;
    for (it_outer = node_cost_map.begin();it_outer != node_cost_map.end();it_outer++) {
        int u = minDistance(dist, visited);
        visited[u] = true;

		std::map<int,long int>::iterator it_inner;
		for (it_inner = (node_cost_map[u]->neighbors).begin();it_inner != (node_cost_map[u]->neighbors).end();it_inner++){
			if (!visited[it_inner->first] && dist[u] != INT_MAX && dist[u] + it_inner->second <= dist[it_inner->first]){
				if (dist[u] + it_inner->second == dist[it_inner->first]){
					int u_parent = u;
					int inner_parent = it_inner->first;

					while (parent[u_parent] != globalMyID){
						u_parent = parent[u_parent];
					}

					while (parent[inner_parent] != globalMyID){
						inner_parent = parent[inner_parent];
					}

					if (u_parent < inner_parent){
						parent[it_inner->first] = u_parent;
					}
				}
				else{
					dist[it_inner->first] = dist[u] + it_inner->second;
					parent[it_inner->first] = u;
				}
			}
		}
    }
 
    // print the constructed distance array
    // printSolution(dist, parent);
}

void broadcastLSA(short int heardFrom){
	std::map<int, LSANode*>::iterator it;
	int num_of_content = 0;
	std::map<int,LSANode> id_node_cost_pair;
	
	for (it = node_cost_map.begin(); it != node_cost_map.end(); it++){
		LSANode t = {it->second->seq, it->second->neighbors};
		std::map<int,LSANode> id_node_cost_pair = {{it->first,t}};
		json lsa_share_json = {
			{"type", "lsa"},
			{"content", id_node_cost_pair}
		};
		std::string map_dump_lsa = lsa_share_json.dump();
		const char* map_dump_lsa_str  = map_dump_lsa.c_str();
		hackyBroadcastNeighbor(map_dump_lsa_str,strlen(map_dump_lsa_str),heardFrom);
	}
}

void broadcastLSASpecific(int dest){
	std::map<int, LSANode*>::iterator it;
	int num_of_content = 0;
	std::map<int,LSANode> id_node_cost_pair;
	
	for (it = node_cost_map.begin(); it != node_cost_map.end(); it++){
		LSANode t = {it->second->seq, it->second->neighbors};
		std::map<int,LSANode> id_node_cost_pair = {{it->first,t}};
		json lsa_share_json = {
			{"type", "lsa"},
			{"content", id_node_cost_pair}
		};
		std::string map_dump_lsa = lsa_share_json.dump();
		const char* map_dump_lsa_str  = map_dump_lsa.c_str();
		hackyBroadcastID(map_dump_lsa_str,strlen(map_dump_lsa_str),dest);
	}
}



void broadcastPartialLSA(int source, short int heardFrom){
	std::map<int,LSANode> id_node_cost_pair = {{source,{node_cost_map[source]->seq, node_cost_map[source]->neighbors}}};
	json lsa_share_json = {
		{"type", "lsa"},
		{"content", id_node_cost_pair}
	};
	std::string map_dump_lsa = lsa_share_json.dump();
	const char* map_dump_lsa_str  = map_dump_lsa.c_str();
	hackyBroadcastNeighbor(map_dump_lsa_str,strlen(map_dump_lsa_str),heardFrom);
}


void* checkingHeartbeats(void* unusedParam){
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 500 * 1000 * 1000; //300 ms
	while(1)
	{
		std::map<int, long int>::iterator it_ht;
		struct timeval tv;
		gettimeofday(&tv,NULL);
		for (it_ht = node_cost_map[globalMyID]->neighbors.begin();it_ht != node_cost_map[globalMyID]->neighbors.end();){
			long int heartbeat_int = (tv.tv_sec - globalLastHeartbeat[it_ht->first].tv_sec) * 1000  + (tv.tv_usec - globalLastHeartbeat[it_ht->first].tv_usec) / 1000 ;
			if (heartbeat_int > 200){
				node_cost_map[globalMyID]->neighbors.erase(it_ht++);
				node_cost_map[globalMyID]->seq += 1;
				broadcastPartialLSA(globalMyID,-1);
			}
			else{
				++it_ht;
			}
		}
		nanosleep(&sleepFor, 0);
	}
}

void* announceToNeighbors(void* unusedParam)
{
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 800 * 1000 * 1000; //300 ms

	while(1)
	{
		hackyBroadcast("H", 1);
		std::map<int, long int>::iterator it_ht;
		struct timeval tv;
		gettimeofday(&tv,NULL);
		for (it_ht = node_cost_map[globalMyID]->neighbors.begin();it_ht != node_cost_map[globalMyID]->neighbors.end();){
			long int heartbeat_int = (tv.tv_sec - globalLastHeartbeat[it_ht->first].tv_sec) * 1000  + (tv.tv_usec - globalLastHeartbeat[it_ht->first].tv_usec) / 1000 ;
			if (heartbeat_int > 1000){
				node_cost_map[globalMyID]->neighbors.erase(it_ht++);
				node_cost_map[globalMyID]->seq += 1;
				broadcastPartialLSA(globalMyID,-1);
			}
			else{
				++it_ht;
			}
		}
		nanosleep(&sleepFor, 0);
	}
}

void broadcastSend(const char* message, int destination, int nextHop){
	int msgLen = 11+sizeof(short int)+strlen(message);
	char sendBuf[msgLen];
	short int no_destID = htons(destination);

	strcpy(sendBuf, "sendForward");
	memcpy(sendBuf+11, &no_destID, sizeof(short int));
	memcpy(sendBuf+11+sizeof(short int), message, strlen(message));
	hackyBroadcastID(sendBuf,msgLen,nextHop);
}

void listenForNeighbors(FILE *logFile)
{
	char fromAddr[100];
	struct sockaddr_in theirAddr;
	socklen_t theirAddrLen;
	unsigned char recvBuf[1000];
	std::map<int,int> parent;
	std::map<int,int> dist;
	int bytesRecvd;
	int curSeq = -1;
	while(1)
	{	
		theirAddrLen = sizeof(theirAddr);
		if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, 1000 , 0, 
					(struct sockaddr*)&theirAddr, &theirAddrLen)) == -1)
		{	
			perror("connectivity listener: recvfrom failed");
			exit(1);
		}
		recvBuf[bytesRecvd] = '\0';
		inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddr, 100);
		short int heardFrom = -1;
		if(strstr(fromAddr, "10.1.1."))
		{
			heardFrom = atoi(
					strchr(strchr(strchr(fromAddr,'.')+1,'.')+1,'.')+1);
			//TODO: this node can consider heardFrom to be directly connected to it; do any such logic now.
			if (node_cost_map[globalMyID]->neighbors.find(heardFrom) == node_cost_map[globalMyID]->neighbors.end()){
				node_cost_map[globalMyID]->neighbors.insert(std::pair<int, long int>(heardFrom, 1));
				node_cost_map[globalMyID]->seq += 1;
				broadcastLSASpecific(heardFrom);
				broadcastPartialLSA(globalMyID,heardFrom);
			}

			//record that we heard from heardFrom just now.
			gettimeofday(&globalLastHeartbeat[heardFrom], 0);
		}

		const char* recvBufConst = (const char*)recvBuf;

		// //Is it a packet from the manager? (see mp2 specification for more details)
		// //send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
		if(!strncmp(recvBufConst, "send", 4)){
			// TODO send the requested message to the requested destination node
			// ...
			int destination;
			const char* message;
			bool forward = false;
			if (!strncmp(recvBufConst+4, "Forward", 7)){
				destination = (recvBufConst[11] << 15) + (recvBufConst[12] & 0xff);
				message = recvBufConst + 13;
				forward = true;
			}
			else{
				destination = (recvBufConst[4] << 8) + (recvBufConst[5] & 0xff);
				message = recvBufConst + 6;
			}

			char logLine[500];
			if (destination == globalMyID){
				sprintf(logLine, "receive packet message %s\n", message);
				fwrite(logLine,1,strlen(logLine),logFile);
				fflush(logFile);
			}
			else if (node_cost_map.find(destination) != node_cost_map.end()){
				djikstra(globalMyID,parent,dist);
				// printSolution(dist,parent);
				int nexthop = destination;

				if (dist[destination] == INT_MAX){
					sprintf(logLine, "unreachable dest %d\n", destination);
					fwrite(logLine,1,strlen(logLine),logFile);
					fflush(logFile);
				}
				else{
					while (parent[nexthop] != globalMyID) {
						nexthop = parent[nexthop];
					}

					if (forward){
						sprintf(logLine, "forward packet dest %d nexthop %d message %s\n", destination, nexthop, message);
					}
					else{
						sprintf(logLine, "sending packet dest %d nexthop %d message %s\n", destination, nexthop, message);
					}
					
					fwrite(logLine,1,strlen(logLine),logFile);
					fflush(logFile);
					broadcastSend(message,destination,nexthop);
				}
			}
			else{
				sprintf(logLine, "unreachable dest %d\n", destination);
				fwrite(logLine,1,strlen(logLine),logFile);
				fflush(logFile);
			}
		}
		else if (!strncmp(recvBufConst,"{",1)){
			json json_recvBuf = json::parse(recvBufConst);
			std::string type = json_recvBuf.at("type").get<std::string>();
			//TODO now check for the various types of packets you use in your own protocol
			if(type == "lsa"){
				std::map<int,LSANode> source_map = json_recvBuf.at("content").get<std::map<int,LSANode>>();
				std::map<int,LSANode>::iterator source_map_it;

				for (source_map_it = source_map.begin(); source_map_it != source_map.end(); source_map_it++){
					int source = source_map_it->first;
					LSANode node_seq_neighbors = source_map_it->second;

					int new_seq = node_seq_neighbors.seq;
					std::map<int,long int> new_neighbors = node_seq_neighbors.neighbors;
					if (node_cost_map.find(source) != node_cost_map.end()){
						if (new_seq > node_cost_map[source]->seq){
							node_cost_map[source]->seq = new_seq;
							node_cost_map[source]->neighbors = new_neighbors;
							broadcastPartialLSA(source,heardFrom);
						}
					}
					else{
						LSANode *t = new LSANode;
						t->seq = new_seq;
						t->neighbors = new_neighbors;
						node_cost_map.insert(std::pair<int,LSANode*>(source,t));
						broadcastPartialLSA(source,heardFrom);
					}
				}
			}
		}
	}
	//(should never reach here)
	close(globalSocketUDP);
}

void process_cost_file(char* filename){
	FILE * fp;
    int node;
	long int cost;

    fp = fopen(filename, "r");
    if (fp == NULL)
        exit(EXIT_FAILURE);

	LSANode *t = new LSANode;
	std::map<int,long int> node_cost;

    while (EOF != fscanf(fp, "%d %ld\n", &node,&cost)){
		node_cost.insert(std::pair<int,long int>(node,cost));
	}
	t->neighbors = node_cost;
	t->seq = 0;
	node_cost_map.insert(std::pair<int,LSANode*>(globalMyID,t));
    fclose(fp);
}