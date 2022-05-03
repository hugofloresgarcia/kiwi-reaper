#include <iostream>
#include <vector>
#include <string>
using namespace std;

// https://www.techiedelight.com/validate-ip-address/
 
// check if a given string is a numeric string or not
bool isNumber(const string &str)
{
    // `std::find_first_not_of` searches the string for the first character
    // that does not match any of the characters specified in its arguments
    return !str.empty() &&
        (str.find_first_not_of("[0123456789]") == std::string::npos);
}
 
// Function to split string `str` using a given delimiter
vector<string> split(const string &str, char delim)
{
    auto i = 0;
    vector<string> list;
 
    auto pos = str.find(delim);
 
    while (pos != string::npos)
    {
        list.push_back(str.substr(i, pos - i));
        i = ++pos;
        pos = str.find(delim, pos);
    }
 
    list.push_back(str.substr(i, str.length()));
 
    return list;
}
 
// Function to validate an IP address
bool validateIP(string ip)
{
    // split the string into tokens
    vector<string> list = split(ip, '.');
 
    // if the token size is not equal to four
    if (list.size() != 4) {
        return false;
    }
 
    // validate each token
    for (string str: list)
    {
        // verify that the string is a number or not, and the numbers
        // are in the valid range
        if (!isNumber(str) || stoi(str) > 255 || stoi(str) < 0) {
            return false;
        }
    }
 
    return true;
}

// #include <stdio.h>
// #include <stdlib.h>
// #include <unistd.h>
// #include <errno.h>
// #include <netdb.h>
// #include <sys/types.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>

// void check_host_name(int hostname) { //This function returns host name for local computer
//    if (hostname == -1) {
//       perror("gethostname");
//       exit(1);
//    }
// }
// void check_host_entry(struct hostent * hostentry) { //find host info from host name
//    if (hostentry == NULL){
//       perror("gethostbyname");
//       exit(1);
//    }
// }
// void IP_formatter(char *IPbuffer) { //convert IP string to dotted decimal format
//    if (NULL == IPbuffer) {
//       perror("inet_ntoa");
//       exit(1);
//    }
// }

// std::string get_local_IP() {
//     char host[256];
//     char *IP;
//     struct hostent *host_entry;
//     int hostname;
//     hostname = gethostname(host, sizeof(host)); //find the host name
//     check_host_name(hostname);
//     host_entry = gethostbyname(host); //find host information
//     check_host_entry(host_entry);
//     IP = inet_ntoa(*((struct in_addr*) host_entry->h_addr_list[0])); //Convert into IP string

//    return std::string(IP);
// }