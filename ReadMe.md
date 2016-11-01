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

