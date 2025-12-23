Made a reliable UDP protocol by implementing basic TCP functionalities from 
scratch.

1) All communication happens through UDP datagrams , hence used SHAM packets
   which stores sequence number, acknowledgement number, flags and window size

2) Sequence Number (seq_num): A 32-bit field indicating the byte-stream number 
   of the first byte in this packet’s data segment.

   Acknowledgment Number (ack_num): A 32-bit field that contains the value 
   of the next sequence number the sender of the ACK is expecting to receive
   .
   
   Flags (flags): A 16-bit field for connection management.
   SYN (Synchronise): 0x1 - Used to initiate a connection.<br>
   ACK (Acknowledge): 0x2 - Indicates the ack_num field is significant.<br>
   FIN (Finish): 0x4 - Used to terminate a connection.<br>
   
   Window Size (window_size): A 16-bit field for flow control. It specifies<br>
   the number of data bytes the sender of this packet is willing to accept.<br>

3) Connection established through 3-way handshake and 4-way handshake (termination)

4) Data Segmentation:file (or user input) is broken into fixed-size chunks <br>
   (1024 bytes). Each chunk is the payload for a S.H.A.M. packet.<br>

   Sliding Window: The sender can transmit multiple packets<br>
   without waiting for an acknowledgment for each one. The number can<br>
   be specified in code on how many packets can be sent without <br>
   acknowledgement.<br>

   Cumulative ACKs: The receiver sends an ACK for the highest<br>
   in-order sequence number received.<br>

   Retransmission Timeout (RTO): The sender maintains a timer for each <br>
   packet sent. If an ACK for a given packet is not received<br>
   within a timeout period (e.g., 500ms), the packet is retransmitted.<br>

5) FLOW CONTROL-
	The receiver must always include its current available buffer space (in bytes) in the window_size field of every packet it sends.
	The sender must read this window_size from incoming ACK packets.
	The sender must ensure that the amount of unacknowledged data it has in<br>
	flight (LastByteSent - LastByteAcked)<br> 
	is always less than or equal to the receiver’s advertised window_size.

6) 2 modes of communication -
   1) File transfer - if chat flag not present, client sends file
      to the server.After successful file transfer,
      the code calculates the MD5 checksum of the received file and prints it to stdout.<br>

   2) Chat mode - After the handshake,both client and server enter a loop to<br>
      handle concurrent input from the keyboard (stdin) and the network socket.<br>
      Typing /quit in the chat initiates the 4-way FIN handshake to terminate<br>
      the connection.<br>

7) Verbose logging included.
   The logging mode activated by setting an environment variable <br>
   RUDP_LOG=1.<br>

COMMAND LINE INTERFACE-----

Your client and server must be executable with the following arguments, <br>
supporting two modes of operation.<br><br>

Server:<br>
./server <port> [--chat] [loss_rate] <br>

Client:<br>
File Transfer Mode (Default)<br>
./client <server_ip> <server_port> <input_file> <output_file_name> [loss_rate]<br><br>

Chat Mode<br>
./client <server_ip> <server_port> --chat [loss_rate]<br>

--chat: An optional flag to activate Chat Mode. When used, all file-related arguments are ignored.<br>

[loss_rate]: An optional floating-point value between 0.0 and 1.0 indicating the packet loss probability.
 Default value=0.0.
