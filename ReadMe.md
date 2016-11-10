10/31/2016
-First create a local memory scanner/writer.
-The goal is to create a server/client memory scanning system where each
	client can scan/rewrite their own memory and a master client
	can scan/rewrite the memory of every client.
-Needed modules: Master client, normal client, server, and a database.
-Relationships: Master client can send requests to the server and receive data.
	Server queries the database for client info and requests scans from
		clients.
	Clients receive requests from the server and send data to the server.
	Every client can scan themselves.

11/9/2016
-Had a lot of difficulties with the c file for the memory scanner with 
	memory locations being unavailable to read using read process mem.
	Tried to use gdb to debug, but there really wasn't anything that 
	could be done with my limited knowledge on the c language.
	To speed things up, I had to start the implementation in c++ 
	ahead of time and convert everything to objects. This presented 
	obstacles on its own due to some segmentation faults which then 
	led me to learn that Cygwin doesn't fully support gdb's core dump 
	reading. Found some potential memory leakage issues later and 
	had issues concerning the data size and mapping the search mask 
	to the buffer of each memory block. Then there were issues with 
	not updating the search mask and matches consistently. However, at 
	the moment, all of these issues have been reduced and the program 
	can successfully narrow down locations in memory based on conditions.
-Next step is to clean up the code and proceed with the rest of the 
	program.
