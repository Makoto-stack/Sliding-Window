#include <iostream>
#include "UdpSocket.h"
#include "Timer.h"
using namespace std;

const int PORT = 74412;       // my UDP port
const int MAX = 20000;        // times of message transfer
const int MAX_WIN = 30;       // maximum window size
const bool verbose = false;   //use verbose mode for more information during run
const int TIMEOUT = 1500;     //1500 usec timeout

// client packet sending functions
void ClientUnreliable(UdpSocket &sock, int max, int message[]);
int ClientStopWait(UdpSocket &sock, int max, int message[]);
int ClientSlidingWindow(UdpSocket &sock, int max, int message[],int windowSize);

// server packet receiving functions
void ServerUnreliable(UdpSocket &sock, int max, int message[]);
void ServerReliable(UdpSocket &sock, int max, int message[]);
void ServerEarlyRetrans(UdpSocket &sock, int max, int message[],int windowSize );

enum myPartType {CLIENT, SERVER} myPart;

int main( int argc, char *argv[] ) 
{
    int message[MSGSIZE/4]; 	  // prepare a 1460-byte message: 1460/4 = 365 ints; 

    // Parse arguments
    if (argc == 1) 
    {
        myPart = SERVER;
    }
    else if (argc == 2)
    {
        myPart = CLIENT;
    }
    else
    {
        cerr << "usage: " << argv[0] << " [serverIpName]" << endl;
        return -1;
    }

    // Set up communication
    // Use different initial ports for client server to allow same box testing
    UdpSocket sock( PORT + myPart );  
    if (myPart == CLIENT)
    {
        if (! sock.setDestAddress(argv[1], PORT + SERVER)) 
        {
            cerr << "cannot find the destination IP name: " << argv[1] << endl;
            return -1;
        }
    }

    int testNumber;
    cerr << "Choose a testcase" << endl;
    cerr << "   1: unreliable test" << endl;
    cerr << "   2: stop-and-wait test" << endl;
    cerr << "   3: sliding windows" << endl;
    cerr << "--> ";
    cin >> testNumber;

    if (myPart == CLIENT) 
    {
        Timer timer;           
        int retransmits = 0;   

        switch(testNumber) 
        {
        case 1:
            timer.Start();
            ClientUnreliable(sock, MAX, message); 
            cout << "Elasped time = ";  
            cout << timer.End( ) << endl;
            break;
        case 2:
            timer.Start();   
            retransmits = ClientStopWait(sock, MAX, message); 
            cout << "Elasped time = "; 
            cout << timer.End( ) << endl;
            cout << "retransmits = " << retransmits << endl;
            break;
        case 3:
            for (int windowSize = 1; windowSize <= MAX_WIN; windowSize++ ) 
            {
	        timer.Start( );
	        retransmits = ClientSlidingWindow(sock, MAX, message, windowSize); 
	        cout << "Window size = ";  
	        cout << windowSize << " ";
	        cout << "Elasped time = "; 
	        cout << timer.End( ) << endl;
	        cout << "retransmits = " << retransmits << endl;
            }
            break;
        default:
            cerr << "no such test case" << endl;
            break;
        }
    }
    if (myPart == SERVER) 
    {
        switch(testNumber) 
        {
            case 1:
                ServerUnreliable(sock, MAX, message); 
                break;
            case 2:
                ServerReliable(sock, MAX, message); 
                break;
            case 3:
                for (int windowSize = 1; windowSize <= MAX_WIN; windowSize++)
                {
	            ServerEarlyRetrans( sock, MAX, message, windowSize ); 
                }
                break;
            default:
                cerr << "no such test case" << endl;
                break;
        }

        // The server should make sure that the last ack has been delivered to client.
        
        if (testNumber != 1)
        {
            if (verbose)
            {
                cerr << "server ending..." << endl;
            }
            for ( int i = 0; i < 10; i++ ) 
            {
                sleep( 1 ); //accepted
                int ack = MAX - 1;
                sock.ackTo( (char *)&ack, sizeof( ack ) );
            }
        }
    }
    cout << "finished" << endl;
    return 0;
}

// Test 1 Client
void ClientUnreliable(UdpSocket &sock, int max, int message[]) //accepted
{
    // transfer message[] max times; message contains sequences number i
    for ( int i = 0; i < max; i++ ) 
    {
        message[0] = i;                            
        sock.sendTo( ( char * )message, MSGSIZE ); 
        if (verbose)
        {
            cerr << "message = " << message[0] << endl;
        }
    }
    cout << max << " messages sent." << endl;
}

// Test1 Server
void ServerUnreliable(UdpSocket &sock, int max, int message[]) 
{
    // receive message[] max times and do not send ack
    for (int i = 0; i < max; i++) 
    {
        sock.recvFrom( ( char * ) message, MSGSIZE );
        if (verbose)
        {  
            cerr << message[0] << endl;
        }                    
    }
    cout << max << " messages received" << endl;
}

//stop and wait client side
int ClientStopWait(UdpSocket &sock, int max, int message[]){
    //retransmits
    int retransmits = 0;
    //prepare a space to receive ack
    int ack;
    //timer
    Timer t;

    //for loop to send message over max iterations
    for (int sequence = 0; sequence < max; ){
        //assign sequence number
        message[0] = sequence;
        //UDP send
        sock.sendTo((char *)message, MSGSIZE);
        
        //start timer 
        t.Start();

        //while acknowledgement not received
        while (sock.pollRecvFrom() < 1){
            //if timeout
            if (t.End() > TIMEOUT){
                //resend
                sock.sendTo((char *) message, MSGSIZE);
                //restart timer
                t.Start();
                //increment retransmits
                retransmits++;
            }
        }

        //receive ack
        ack = sock.recvFrom((char *)message, MSGSIZE);
        sequence++;
    }

    //return total retransmits
    return retransmits;
}

//sliding window client side
int ClientSlidingWindow(UdpSocket &sock, int max, int message[], int windowSize){
    //retransmits
    int retransmits = 0;
    //receive ack
    int ack = -1;
    //expected seqnum of next ack
    int ackSeq = 0;
    //timer
    Timer t;

    //for loop to send messages over max iterations
    for (int sequence = 0; sequence < max || ackSeq < max; ){
        //within the sliding window
        if ( (ackSeq + windowSize) > sequence && sequence < max){
            //assign sequence number
            message[0] = sequence;
            //UDP Send
            sock.sendTo((char *)message, MSGSIZE);
            //if ack arrives and matches ackSeq, increment ackSeq
            if (sock.pollRecvFrom() > 0){
                sock.recvFrom((char *) &ack, sizeof(ack));
                if (ack == ackSeq){
                    ackSeq++;
                }
            }
            //increment sequence
            sequence++;
        }
        //the sliding window is full!
        else{
            //start timer
            t.Start();
            //while acknowledgement not received
            while(sock.pollRecvFrom() < 1){
                //if timeout
                if(t.End() > TIMEOUT){
                    //increment retransmits
                    retransmits += sequence - ackSeq;
                    //set seq to ackSeq and break
                    sequence = ackSeq;
                    break;
                }
            }
            //receive ack
            sock.recvFrom((char *) &ack, sizeof(ack));
            //check if ack exceeds ackSeq
            if (ack >= ackSeq){
                //set ackSeq accordingly
                ackSeq = ack + 1;
            }
        }
    }
    //return total retransmits
    return retransmits;
}

//stop and wait server side
void ServerReliable(UdpSocket &sock, int max, int message[])
{
    //an ack message
    int ack;

    //receive message[] max times
    for (int sequence = 0; sequence < max; ){
        //UDP message receive
        sock.recvFrom((char *)message, MSGSIZE);

        //loop until message received
        while (message[0] != sequence){
            sock.recvFrom((char *)message, MSGSIZE);
        }

        //send ack
        ack = sequence;
        sock.ackTo((char *)&ack, sizeof(ack));
        //increment sequence
        sequence++;
    }
    return;
}

/**
 * Server side for sliding window algorithm
 */
void ServerEarlyRetrans(UdpSocket &sock, int max, int message[],int windowSize ){
    int ack;                //an ack message
    bool array[max];        //indicates the arrival of message[i]
    int sequence;           //sequence# of message expected
    for (int j = 0; j < max; j++){
        array[j] = false;
    }

    for (sequence = 0; sequence < max;){
        //UDP message receive
        sock.recvFrom((char *)message, MSGSIZE);
        
        //if message[0] matches seq
        if (message[0] == sequence){
            //mark as received
            array[sequence] = true;
            //scan for cumulative ack
            while (array[sequence]){
                sequence++;
            }
        }
        
        else{
            //mark array[message[0]] as being received
            if (message[0] <= windowSize + sequence){
                array[message[0]] = true;
            }
        }
        //send back the cumulative ack for the series of messages received so far
        ack = sequence-1;
        sock.ackTo((char *)&ack, sizeof(ack));
    }
    return;
}