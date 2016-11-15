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

11/12/2016
-Some minor bug fixes were worked out and the peek/poke (read/write) 
	functions were implemented. Starting work on the basic UI.

11/14/2016
-UI has most functionality other than poking/peeking. 
	Strange bug has occurred where the last memory block isn't 
	formed that seems to be due to the lack of an additional
	istream object. Still unsure about why adding an istream object
	causes the last memory block to be formed in a scan.

11/15/2016
-Found the missing memory block by initializing the initial address
	when creating a scan to 0 rather than letting the computer 
	assign it. Wrote up the overwrite functionality, tested it on
	solitaire and it worked! Have to be careful with integer limits
	though as the max on windows is 2.14 million. Need to smooth out
	the program now. The program can be a bit tedious to use since
	filters need to be constantly applied to narrow down the search
	and there's currently no way to know how many searches would come 
	up when printed. It'd also be nice to figure out a better ui overall.
