#include "common.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
#include "common.h"
#include "HistogramCollection.h"
#include "FIFOreqchannel.h"
using namespace std;


void patient_function(BoundedBuffer* buffer, int patientNum, int requests)
{
    /* What will the patient threads do? Pushes patient messages into bounded buffer*/ 
    for(double i = 0.0; i < requests; i++)
    {
        datamsg* msg = new datamsg(patientNum, i*0.004, 1);
        char* charMsg = (char*) msg;
        vector<char> requestVec(charMsg, charMsg + sizeof(datamsg)); // why does this have to be a vector?
        buffer->push(requestVec);
    }
}

void worker_function(BoundedBuffer* buffer, FIFORequestChannel* chan)
{
    /*
		Functionality of the worker threads: pops off messages from bounded buffer and send them to server
    */
   while(true)
   {
        vector<char> poppedVec = buffer->pop();
        char* req = reinterpret_cast<char*>(poppedVec.data());
        MESSAGE_TYPE message = *(MESSAGE_TYPE*) req;
        switch (message)
        {
            case DATA_MSG:
            {
               chan->cwrite(req, sizeof(datamsg));
               char* response = chan->cread();
               // histogram

            }
            break;
            case QUIT_MSG:
            {
                chan->cwrite(req, sizeof(QUIT_MSG));
            }
            return;
        }
   }
    
}
int main(int argc, char *argv[])
{
    int n = 100;    //default number of requests per "patient"
    int p = 10;     // number of patients [1,15]
    int w = 100;    //default number of worker threads
    int b = 20; 	// default capacity of the request buffer, you should change this default
	int m = MAX_MESSAGE; 	// default capacity of the file buffer
    srand(time_t(NULL));
    
    
    int pid = fork();
    if (pid == 0){
		// modify this to pass along m
        execl ("dataserver", "dataserver", (char *)NULL);
        
    }
    
	FIFORequestChannel* chan = new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE);
    BoundedBuffer request_buffer(b);
	HistogramCollection hc; // pass into worker function
	
    struct timeval start, end;
    gettimeofday (&start, 0);

    /* Start all threads here */

    // list of threads
    vector<thread> patientThreads;
    vector<thread> workerThreads;

    // populate threads
    for(int i = 0; i < p; i++)
    {
        patientThreads.push_back(move(thread(&patient_function,&request_buffer,i+1,n)));
    }

    for(int i=0; i < w; i++)
    {
        MESSAGE_TYPE n = NEWCHANNEL_MSG;
        chan->cwrite ((char *) &n, sizeof (MESSAGE_TYPE));
        char* response = chan->cread();
        FIFORequestChannel* workerChan = new FIFORequestChannel(response, FIFORequestChannel::CLIENT_SIDE);
        workerThreads.push_back(move(thread(&worker_function, &request_buffer, workerChan)));
    }


	/* Join all threads here */
    
    // join patient threads
    for(int i = 0; i < p; i++)
    {
        patientThreads[i].join();
    }

    // send quit messages to buffer
    for(int i = 0; i < w; i++)
    {
        //vector<char> quit;
        MESSAGE_TYPE quit = QUIT_MSG;
        char* quitMessage = (char*) &quit;
        vector<char> quitReq = vector<char>(quitMessage, quitMessage+sizeof(QUIT_MSG));
        request_buffer.push(quitReq); 
    }

    // join worker threads
    for(int i=0; i < w; i++)
    {
        workerThreads[i].join();
    }

    gettimeofday (&end, 0);
	hc.print ();
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took " << secs << " seconds and " << usecs << " micor seconds" << endl;

    MESSAGE_TYPE q = QUIT_MSG;
    chan->cwrite ((char *) &q, sizeof (MESSAGE_TYPE));
    cout << "All Done!!!" << endl;
    delete chan;
    
}
