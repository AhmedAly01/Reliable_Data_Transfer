#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string>
#include <bits/stdc++.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <poll.h>

using namespace std;

#define maxSegSize 508

struct packet
{
    uint16_t checkSum;
    uint16_t len;
    uint32_t seqNo;
    char data[500];
};

struct packetAck
{
    uint16_t checkSum;
    uint16_t len;
    uint32_t ackNo;
};

/*
   read Commands from info.txt
*/
vector<string> readInfo()
{
    string fName = "info.txt";
    vector<string> infos;
    string line;
    ifstream f;
    f.open(fName);
    while (getline(f, line))
    {
        infos.push_back(line);
    }
    return infos;
}

/*
   this function is used to write file
*/
void writeFile(string fName, string data)
{
    ofstream f_stream(fName.c_str());
    f_stream.write(data.c_str(), data.length());
}

/*
  this function is used to getAckCheck sum
*/
uint16_t getAckChecksum(uint16_t len, uint32_t ackNo)
{
    uint32_t sum = 0;
    sum += len;
    sum += ackNo;
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    uint16_t Sum = (uint16_t)(~sum);
    return Sum;
}

/*
   this function is used to get data check sum;
*/
uint16_t getDataChecksum(string content, uint16_t len, uint32_t seqNo)
{
    uint32_t sum = 0;
    sum += len;
    sum += seqNo;
    char a[content.length() + 1];
    strcpy(a, content.c_str());
    for (int i = 0; i < content.length(); i++)
    {
        sum += a[i];
    }
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

/*
 this function is used to create packet
*/
packet createPacket(string fName)
{
    struct packet p;
    strcpy(p.data, fName.c_str());
    p.checkSum = 0;
    p.seqNo = 0;
    p.len = sizeof(p.checkSum) + sizeof(p.len) + sizeof(p.seqNo) + fName.length();
    return p;
}

/*
  this function of send Ack
*/
void sendAck(int clientSocket, struct sockaddr_in serverAddress, int seqNo)
{
    struct packetAck pAck;
    pAck.ackNo = seqNo;
    pAck.len = sizeof(pAck);
    pAck.checkSum = getAckChecksum(pAck.len, pAck.ackNo);
    char *bufferAck = new char[maxSegSize];
    memset(bufferAck, 0, maxSegSize);
    memcpy(bufferAck, &pAck, sizeof(pAck));
    ssize_t bytesSent = sendto(clientSocket, bufferAck, maxSegSize, 0, (struct sockaddr *)&serverAddress, sizeof(struct sockaddr));
    if (bytesSent == -1)
    {
        cout << "Sending packet failure" << endl;
        exit(1);
    }
    else
    {
        cout << "Ack for packet with seq. number " << seqNo << " is sent successfully" << endl;
    }
}

int main()
{
    vector<string> infos = readInfo();
    string ipAdr = infos[0];
    int port = stoi(infos[1]);
    string fName = infos[2];
    struct sockaddr_in serverAddress;
    int clientSocket;
    memset(&clientSocket, '0', sizeof(clientSocket));
    if ((clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
        cout << "failure" << endl;
        return 1;
    }
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);
    cout << "File Name: " << fName << " size: " << fName.size() << endl;
    struct packet fName_packet = createPacket(fName);
    char *buffer = new char[maxSegSize];
    memset(buffer, 0, maxSegSize);
    memcpy(buffer, &fName_packet, sizeof(fName_packet));
    ssize_t bytesSent = sendto(clientSocket, buffer, maxSegSize, 0, (struct sockaddr *)&serverAddress, sizeof(struct sockaddr));
    if (bytesSent == -1)
    {
        cout << "Sending file name failure" << endl;
        return 2;
    }
    else
    {
        cout << "File name sent" << endl;
    }
    char recBuffer[maxSegSize];
    socklen_t adrLen = sizeof(serverAddress);
    struct pollfd pfd = {.fd = clientSocket, .events = POLLIN};
    int activity = poll(&pfd, 1, 5000);
    if (activity < 0)
    {
        perror("TIMEOUT!");
    }
    ssize_t receivedBytes = recvfrom(clientSocket, recBuffer, maxSegSize, 0, (struct sockaddr *)&serverAddress, &adrLen);
    if (receivedBytes < 0)
    {
        cout << "Sending filename acknowlegment failure";
        return 3;
    }
    auto *ackPacket = (struct packetAck *)recBuffer;
    cout << "Number of packets: " << ackPacket->len << endl;
    long packetsNum = ackPacket->len;
    string fData[packetsNum];
    bool recieved[packetsNum];
    int idx = 1;
    while (idx <= packetsNum)
    {
        memset(recBuffer, 0, maxSegSize);
        ssize_t bytesReceived = recvfrom(clientSocket, recBuffer, maxSegSize, 0, (struct sockaddr *)&serverAddress, &adrLen);
        if (bytesReceived == -1)
        {
            cout << "Packet recieving failure" << endl;
            break;
        }
        auto *pac = (struct packet *)recBuffer;
        cout << "packet number: " << idx << " received successfully" << endl;
        cout << "Sequence number: " << pac->seqNo << endl;
        int len = pac->len;
        for (int j = 0; j < len; j++)
        {
            fData[pac->seqNo] += pac->data[j];
        }
        if (getDataChecksum(fData[pac->seqNo], pac->len, pac->seqNo) != pac->checkSum)
        {
            cout << "Packet data is corrupted" << endl;
        }
        sendAck(clientSocket, serverAddress, pac->seqNo);
        idx++;
    }
    string content = "";
    for (int i = 0; i < packetsNum; i++)
    {
        content += fData[i];
    }
    writeFile(fName, content);
    cout << "File is sent successfully" << endl;
    return 0;
}