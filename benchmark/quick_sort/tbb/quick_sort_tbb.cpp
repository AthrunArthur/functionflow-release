#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>
#include <tbb/parallel_invoke.h>
#include "tbb/task_scheduler_init.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#ifdef CACHE_EVAL
#include <papi.h>
#endif
#include <assert.h>

#define BUFFER_LEN 100000
#define INCREMENT 1000
#define SEPARATOR ','

using namespace std;
using namespace tbb;

/* partition -- Partition the data from start to end to two parts
 * with the left part is less than the pivot, and the right part is larger.
 * the return value is the index of the partition pivot */
int partition(int *data,int start,int end)
{
    int pivot;
    int i, j;
    int tmp;
    pivot = data[end];
    i = start-1;
    for(j=start; j<end; j++)
        if(data[j]<=pivot)
        {
            i++;/* i is the number of data less than the pivot before the index j. */
            tmp=data[i];
            data[i]=data[j];
            data[j]=tmp;
        }
    tmp=data[i+1];
    data[i+1]=data[end];
    data[end]=tmp;
    /* data[i+1]=pivot */
    return i+1;
}

/* quick_sort -- Use the serial quick sort algorithm to sort data. */

void quick_sort(int *data,int start,int end)
{
    int r;
    int i;
    if(start < end)
    {
        r = partition(data,start,end);
        quick_sort(data,start,r-1);
        quick_sort(data,r+1,end);
    }
}


/* parallel quick sort using ff::para. */
void para_quick_sort(int * data,int i,int j,int para_len)
{

    int r;
    if(j-i <= para_len)/*The length is too small.*/
        quick_sort(data,i,j);
    else
    {
        r = partition(data,i,j);
        tbb::parallel_invoke(
        [&data,&i,&r,&para_len]() {
            para_quick_sort(data,i,r-1,para_len);
        },
        [&data,&r,&j,&para_len]() {
            para_quick_sort(data,r+1,j,para_len);
        }
        );
    }
}

int main(int argc, char *argv[])
{
    task_scheduler_init init(8);
    boost::property_tree::ptree pt;
    pt.put("time-unit", "us");
//     string in_file_name = "../quick_sort/ff/numbers.txt";
    string in_file_name = "../ff/numbers.txt";
    string out_file_name = "numbers_sort.txt";
    string time_file_name = "para_time.txt";
    int len,i;
    ifstream in_file;
    ofstream out_file,out_time_file;
    int n_div = 1;// Default 1
    int para_len;
    int * data = (int *)malloc(sizeof(int)*BUFFER_LEN),buf_size = BUFFER_LEN;

    if(argc > 1) {
        stringstream ss_argv;
        ss_argv << argv[1];
        ss_argv >> n_div;
    }

    if(in_file_name.empty() || out_file_name.empty() || time_file_name.empty()) {
        cout << "File name is empty!" << endl;
        return -1;
    }
    else {
        in_file.open(in_file_name.c_str());
        out_file.open(out_file_name.c_str());
    }
    if(!in_file.is_open()) {
        cout << "Can't open the file " << in_file_name << endl;
        return -1;
    }
    if(!out_file.is_open()) {
        cout << "Can't open the file " << out_file_name << endl;
        return -1;
    }
    for(i=0; !in_file.eof(); i++) {
      char tmp;
      if(i >= buf_size){
	buf_size += INCREMENT;
	data = (int *)realloc(data,buf_size*sizeof(int));
// 	cout << "New size = " << buf_size << endl;
      }        
        in_file >> data[i];
        in_file.get(tmp);
        if(tmp != SEPARATOR) {
            i++;
            break;
        }
    }
    if(!in_file.eof() && i > buf_size) {
        cout << "Max data length is " << buf_size << endl;
        cout << "Only the first " << buf_size << " numbers will be sorted" << endl;
    }
    len = i;
    para_len = len / n_div;
    if(n_div == 1)
        cout << "len = " << len << endl;
    else
        cout << "Para granularity = " << n_div << endl;
    in_file.close();

#   ifdef CACHE_EVAL
    /*Add papi to trace cache miss*/
    int EventSet,retVal;
    long long startRecords[2], endRecords[2];
    retVal = PAPI_library_init(PAPI_VER_CURRENT);
    assert(retVal == PAPI_VER_CURRENT);
    EventSet = PAPI_NULL;
    retVal = PAPI_create_eventset(&EventSet);
    assert(retVal == PAPI_OK);
    //L1 TCM & TCA
    retVal = PAPI_add_event(EventSet, PAPI_L1_TCM);
    assert(retVal == PAPI_OK);
    retVal = PAPI_add_event(EventSet, PAPI_L1_TCA);
    assert(retVal == PAPI_OK);
    
    //L2 TCM & TCA
//     retVal = PAPI_add_event(EventSet, PAPI_L2_TCM);
//     assert(retVal == PAPI_OK);
//     retVal = PAPI_add_event(EventSet, PAPI_L2_TCA);
//     assert(retVal == PAPI_OK);
    
    //L3 TCM & TCA
//     retVal = PAPI_add_event(EventSet, PAPI_L3_TCM);
//     assert(retVal == PAPI_OK);
//     retVal = PAPI_add_event(EventSet, PAPI_L3_TCA);
//     assert(retVal == PAPI_OK);    
    
    retVal = PAPI_start(EventSet);
    assert(retVal == PAPI_OK);
    retVal = PAPI_read(EventSet, startRecords);
    assert(retVal == PAPI_OK);
    /*Add papi to trace cache miss*/
#   endif

    chrono::time_point<chrono::system_clock> start, end;
    start = chrono::system_clock::now();

    para_quick_sort(data,0,len-1,para_len);


    end = chrono::system_clock::now();
    int elapsed_seconds = chrono::duration_cast<chrono::microseconds>
                          (end-start).count();
    if(n_div != 1)
        pt.put("para-elapsed-time", elapsed_seconds);
    else
        pt.put("sequential-elapsed-time", elapsed_seconds);
    boost::property_tree::write_json("time.json", pt);
 
#   ifdef CACHE_EVAL
    /*Stop papi trace*/
    retVal = PAPI_stop(EventSet, endRecords);
    assert(retVal == PAPI_OK);
    retVal = PAPI_cleanup_eventset(EventSet);
    assert(retVal == PAPI_OK);
    retVal = PAPI_destroy_eventset(&EventSet);
    assert(retVal == PAPI_OK);
    PAPI_shutdown(); 
    //L1 result
    std::cout << "L1 total cache miss = " << endRecords[0] - startRecords[0] << std::endl;
    std::cout << "L1 total cache access = " << endRecords[1] - startRecords[1] << std::endl;
    //L2 result
//     std::cout << "L2 total cache miss = " << endRecords[0] - startRecords[0] << std::endl;
//     std::cout << "L2 total cache access = " << endRecords[0] - startRecords[0] << std::endl;
    //L3 result
//     std::cout << "L3 total cache miss = " << endRecords[0] - startRecords[0] << std::endl;
//     std::cout << "L3 total cache access = " << endRecords[0] - startRecords[0] << std::endl;
    /*Stop papi trace*/
#   endif
    
    cout << "Elapsed time: " << elapsed_seconds << "us" << endl;
    //print results:
//     for(i=0; i<len; i++) {
//         out_file << data[i];
//         if(i < len - 1) {
//             out_file << SEPARATOR;
//         }
//     }
//     out_file << endl << "Elapsed time: " << elapsed_seconds << "us" << endl;
    out_file.close();

    if(n_div != 1) {
        out_time_file.open(time_file_name.c_str(),ios::app);
        if(!out_time_file.is_open()) {
            cout << "Can't open the file " << time_file_name << endl;
            return -1;
        }
        out_time_file << elapsed_seconds << endl;
        out_time_file.close();
    }
    else {
        out_time_file.open("time.txt");
        if(!out_time_file.is_open()) {
            cout << "Can't open the file time.txt" << endl;
            return -1;
        }
        out_time_file << elapsed_seconds << endl;
        out_time_file.close();
    }

    return 0;//return the parallel time to the system

}
