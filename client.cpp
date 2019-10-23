#include "common.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
#include "common.h"
#include "HistogramCollection.h"
#include "FIFOreqchannel.h"
using namespace std;

// for requesting data points (part 1)
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

// for file requests (part 2)

void patient_function2(BoundedBuffer* buffer, __int64_t size, string fileName, int packet){

    int fd = open(("./received/x"+fileName).c_str(), O_CREAT | O_TRUNC | O_WRONLY | ios::out | ios::binary, 0777);
    close(fd);

    // push packet requests into bounded buffer
    char requestArr[sizeof(filemsg) + fileName.length() + 1];
    const char* fileString = fileName.c_str();
    filemsg* msg;

    int packetSize = packet;
    int iters = size / packetSize;
    int endCase = size % packetSize;
    int i;

    for(i = 0; i <= iters; i++)
    {
        if(size <= i*packetSize)
        {
            break;
        }
        if(size % packetSize !=0 && i == iters)
        {
            msg = new filemsg(i*packetSize, endCase);
        }
        else
        {
            msg = new filemsg(i*packetSize, packetSize);
        }
        memcpy(&requestArr, msg, sizeof(filemsg));
        strcpy(requestArr + sizeof(filemsg), fileString);
        vector<char> requestVec(requestArr, requestArr + sizeof(requestArr));
        buffer->push(requestVec);
    }
}

void worker_function(BoundedBuffer* buffer, FIFORequestChannel* chan, HistogramCollection* hc, string fileName)
{
    /*
		Functionality of the worker threads: pops off messages from bounded buffer and send them to server
    */
   int fd = open(("./received/x"+fileName).c_str(), O_WRONLY | ios::out | ios::binary, 0777);

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
               
               // update histogram
               datamsg* type = (datamsg*) req;
               hc->update(type->person, *(double*) response);

            }
            break;
            case FILE_MSG:
            {
                // cast char* back to file message* (to get offset and length you are trying to write)
                filemsg* msg = (filemsg*) req; 
                chan->cwrite(req, sizeof(filemsg) + fileName.length() + 1);
                // cread the data
                char* response = chan->cread();
                // lseek
                lseek(fd, msg->offset, SEEK_SET);
                // // write
                write(fd, response ,msg->length);
            }
            break;
            case QUIT_MSG:
            {
                chan->cwrite(req, sizeof(QUIT_MSG));
                delete chan;
            }
            return;
        }
   }
   close(fd);
}
int main(int argc, char *argv[])
{
    int n = 10;    //default number of requests per "patient"
    int p = 10;     // number of patients [1,15]
    int w = 500;    //default number of worker threads
    int b = 20; 	// default capacity of the request buffer, you should change this default
	int m = MAX_MESSAGE; 	// default capacity of the file buffer
    srand(time_t(NULL));
    
    string f = "";

    int opt;
    while((opt = getopt(argc, argv, "n:p:w:b:f:")) != -1)  
    {  
        switch(opt)
        {
            case 'n':
                if(optarg){
                    n = stoi(optarg);
                    }
                break;
            case 'p':
                if(optarg)
                {
                    p = stoi(optarg);
                }
                break;
            case 'w':
                if(optarg){
                    w = stoi(optarg);
                }
                break;
            case 'b':
                if(optarg){
                    b = stoi(optarg);
                }
                break;
            case 'f':
                if(optarg) {
                    // check to see if file is in directory?? (fstream might handle it)
                    if(optarg)
                    {
                        f = optarg;
                    }
                }
                break;
            default:
                abort();
        }
    }
    
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

    // ------------------------- part 1 ------------------------------- // change parameters

    if(f == "")
    {
        // dummy fileName
        string fileName = "";
        // list of threads
        vector<thread> patientThreads;
        vector<thread> workerThreads;

        // create histograms and add to histo list
        for(int i = 0; i < p; i++)
        {
            Histogram* hist = new Histogram(p,-2,2);
            hc.add(hist);
        }

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
            workerThreads.push_back(move(thread(&worker_function, &request_buffer, workerChan, &hc, fileName)));
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
    }
    
    // ------------------------- part 2 -------------------

    if(f != "")
    {
        // prepare request array to be sent to server for FileSize
        string fileName = f;

        const char* fileString = fileName.c_str();
        char* requestArr = new char[sizeof(filemsg) + fileName.length() + 1];

        filemsg* msg = new filemsg(0, 0);

        memcpy(requestArr, msg, sizeof(filemsg));
        strcpy(requestArr + sizeof(filemsg), fileString);

        chan->cwrite(requestArr, sizeof(filemsg) + fileName.length() + 1);

        char* resp = chan->cread();
        __int64_t size = *(__int64_t*)resp;

        // --------- create threads ----------

        vector<thread> workerThreads;

        // open file descriptor

        thread patientThread = thread(&patient_function2, &request_buffer, size, fileString, m);

        for(int i=0; i < w; i++)
        {
            MESSAGE_TYPE n = NEWCHANNEL_MSG;
            chan->cwrite ((char *) &n, sizeof (MESSAGE_TYPE));
            char* response = chan->cread();
            FIFORequestChannel* workerChan = new FIFORequestChannel(response, FIFORequestChannel::CLIENT_SIDE);
            workerThreads.push_back(move(thread(&worker_function, &request_buffer, workerChan, &hc, fileName)));
        }

        // join threads
        patientThread.join();

        // send quit messages to buffer
        for(int i = 0; i < w; i++)
        {
            //vector<char> quit;
            MESSAGE_TYPE quit = QUIT_MSG;
            char* quitMessage = (char*) &quit;
            vector<char> quitReq = vector<char>(quitMessage, quitMessage+sizeof(QUIT_MSG));
            request_buffer.push(quitReq); 
        }

        for(int i=0; i < w; i++)
        {
            workerThreads[i].join();
        }
    }

// ------------------------------

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
